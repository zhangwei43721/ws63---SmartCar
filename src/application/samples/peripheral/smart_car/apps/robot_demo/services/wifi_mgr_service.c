/**
 * @file wifi_mgr_service.c
 * @brief WiFi Manager：消息队列驱动状态机，按需启动 STA/AP task
 */

#include "wifi_mgr_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "../../../drivers/wifi_client/bsp_wifi_ap.h"
#include "securec.h"
#include "soc_osal.h"
#include "storage_service.h"

// ---------- 配置 ----------
#define MGR_STACK_SIZE 2048
#define MGR_PRIO 20
#define STA_FAIL_MAX 1

// ---------- Manager 私有状态 ----------
typedef enum {
    MGR_STATE_IDLE = 0,
    MGR_STATE_STA,
    MGR_STATE_AP,
} mgr_state_t;

static mgr_state_t s_mgr_state = MGR_STATE_IDLE;
static osal_task *s_mgr_task = NULL;
static unsigned long s_msg_queue = 0;
static int s_sta_fail_cnt = 0;

// ---------- 事件回调（写入队列唤醒 Manager） ----------
static void wifi_event_cb(bsp_wifi_event_t event, void *arg)
{
    (void)arg;
    printf("[WiFi Mgr CB] event=%d\r\n", (int)event);
    bsp_wifi_msg_t msg = {.id = WIFI_MSG_START};
    switch (event) {
        case BSP_WIFI_EVENT_STA_GOT_IP:
            msg.id = WIFI_MSG_STA_GOT_IP;
            break;
        case BSP_WIFI_EVENT_STA_FAIL:
            msg.id = WIFI_MSG_STA_FAIL;
            break;
        case BSP_WIFI_EVENT_STA_STOPPED:
            msg.id = WIFI_MSG_STA_STOPPED;
            break;
        case BSP_WIFI_EVENT_AP_READY:
            msg.id = WIFI_MSG_AP_READY;
            break;
        case BSP_WIFI_EVENT_AP_STOPPED:
            msg.id = WIFI_MSG_AP_STOPPED;
            break;
        default:
            return;
    }
    int ret = bsp_wifi_mgr_send_msg(&msg);
    printf("[WiFi Mgr CB] send msg.id=%d ret=%d\r\n", (int)msg.id, ret);
}

// ---------- 状态机处理 ----------

static void on_start(void)
{
    char ssid[32] = {0}, pwd[64] = {0};
    storage_service_get_wifi_config(ssid, pwd);

    if (ssid[0] == '\0') {
        printf("[WiFi Mgr] 无配置，启动 AP\r\n");
        sta_task_stop();
        if (ap_task_start())
            s_mgr_state = MGR_STATE_AP;
    } else {
        printf("[WiFi Mgr] 配置 SSID=%s，启动 STA\r\n", ssid);
        ap_task_stop();
        s_sta_fail_cnt = 0;
        bsp_wifi_set_current_config(ssid, pwd);
        if (sta_task_start())
            s_mgr_state = MGR_STATE_STA;
    }
}

static void on_sta_got_ip(void)
{
    s_sta_fail_cnt = 0;
    printf("[WiFi Mgr] STA 已连接\r\n");
}

static void on_sta_fail(void)
{
    if (s_mgr_state != MGR_STATE_STA)
        return;
    s_sta_fail_cnt++;
    printf("[WiFi Mgr] STA 失败 %d/%d\r\n", s_sta_fail_cnt, STA_FAIL_MAX);
    if (s_sta_fail_cnt >= STA_FAIL_MAX) {
        printf("[WiFi Mgr] STA 连续失败，切换 AP\r\n");
        sta_task_stop();
        if (ap_task_start())
            s_mgr_state = MGR_STATE_AP;
    }
}

static void on_ap_ready(void)
{
    s_sta_fail_cnt = 0;
    printf("[WiFi Mgr] AP 就绪\r\n");
}

static int mgr_task_main(void *arg)
{
    (void)arg;
    printf("[WiFi Mgr] 启动\r\n");

    // 注册事件回调
    bsp_wifi_register_event_cb(wifi_event_cb, NULL);

    while (1) {
        bsp_wifi_msg_t msg;
        unsigned int sz = sizeof(msg);
        // 纯事件驱动：消息队列永久阻塞，无消息时让出 CPU
        if (osal_msg_queue_read_copy(s_msg_queue, &msg, &sz, OSAL_WAIT_FOREVER) != OSAL_SUCCESS)
            continue;

        printf("[WiFi Mgr] 处理消息 msg.id=%d\r\n", msg.id);
        switch (msg.id) {
            case WIFI_MSG_START:
                on_start();
                break;
            case WIFI_MSG_STA_GOT_IP:
                on_sta_got_ip();
                break;
            case WIFI_MSG_STA_FAIL:
                on_sta_fail();
                break;
            case WIFI_MSG_STA_STOPPED:
                break;
            case WIFI_MSG_AP_READY:
                on_ap_ready();
                break;
            case WIFI_MSG_AP_STOPPED:
                break;
            default:
                break;
        }
    }
    return 0;
}

// ---------- 对外接口 ----------

int bsp_wifi_mgr_init(void)
{
    if (s_mgr_task)
        return 0;

    // 队列深度 4：与 robot_main_task 保持一致，规避深度为 1 时的潜在唤醒异常
    if (osal_msg_queue_create("wifi_mgr", 8, &s_msg_queue, 0, sizeof(bsp_wifi_msg_t)) != OSAL_SUCCESS) {
        printf("[WiFi Mgr] 队列创建失败\r\n");
        return -1;
    }

    osal_kthread_lock();
    s_mgr_task = osal_kthread_create((osal_kthread_handler)mgr_task_main, NULL, "wifi_mgr", MGR_STACK_SIZE);
    if (s_mgr_task)
        osal_kthread_set_priority(s_mgr_task, MGR_PRIO);
    osal_kthread_unlock();

    if (!s_mgr_task) {
        printf("[WiFi Mgr] task 创建失败\r\n");
        return -1;
    }
    printf("[WiFi Mgr] 初始化完成\r\n");
    return 0;
}

int bsp_wifi_mgr_send_msg(const bsp_wifi_msg_t *msg)
{
    if (s_msg_queue == 0)
        return -1;

    // 有旧消息则丢弃，确保新事件能写入（完全复刻 motor_executor_push_cmd）
    unsigned int msg_num = osal_msg_queue_get_msg_num(s_msg_queue);
    if (msg_num > 0) {
        bsp_wifi_msg_t dummy;
        unsigned int sz = sizeof(dummy);
        osal_msg_queue_read_copy(s_msg_queue, &dummy, &sz, OSAL_MSGQ_NO_WAIT);
    }

    int ret = osal_msg_queue_write_copy(s_msg_queue, (void *)msg, sizeof(*msg), OSAL_MSGQ_NO_WAIT);
    return (ret == OSAL_SUCCESS) ? 0 : -1;
}

// ---------- 向后兼容 ----------

int bsp_wifi_smart_init(void)
{
    if (bsp_wifi_mgr_init() != 0)
        return -1;
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START});
}

int bsp_wifi_connect_ap(const char *ssid, const char *password)
{
    if (ssid && password)
        storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START});
}

int bsp_wifi_switch_to_ap(void)
{
    storage_service_save_wifi_config("", "");
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START});
}
