/**
 * @file wifi_mgr_service.c
 * @brief WiFi Manager：STA 挂起待命 + AP 按需精简版
 */

#include "wifi_mgr_service.h"
#include <stdio.h>
#include <string.h>
#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "../../../drivers/wifi_client/bsp_wifi_ap.h"
#include "../robot_common.h"
#include "securec.h"
#include "soc_osal.h"
#include "../../../platform/storage_service.h"
#include "captive_portal_service.h"

#define MGR_STACK_SIZE 2048
#define MGR_PRIO 20

static osal_task *s_mgr_task = NULL;
static unsigned long s_msg_queue = 0;
static bool s_sta_from_portal = false;
static bool s_user_initiated = false;
static char s_prev_ssid[32] = {0};
static char s_prev_pwd[64] = {0};

static void wifi_event_cb(bsp_wifi_event_t event, void *arg)
{
    (void)arg;
    bsp_wifi_msg_t msg = {0};
    switch (event) {
        case BSP_WIFI_EVENT_STA_GOT_IP:   msg.id = WIFI_MSG_STA_GOT_IP;   break;
        case BSP_WIFI_EVENT_STA_FAIL:     msg.id = WIFI_MSG_STA_FAIL;     break;
        case BSP_WIFI_EVENT_STA_STOPPED:  msg.id = WIFI_MSG_STA_STOPPED;  break;
        case BSP_WIFI_EVENT_AP_READY:     msg.id = WIFI_MSG_AP_READY;     break;
        case BSP_WIFI_EVENT_AP_STOPPED:   msg.id = WIFI_MSG_AP_STOPPED;   break;
        default: return;
    }
    (void)bsp_wifi_mgr_send_msg(&msg);
}

static int mgr_task_main(void *arg)
{
    (void)arg;
    printf("[WiFi Mgr] 启动\r\n");
    bsp_wifi_register_event_cb(wifi_event_cb, NULL);

    while (1) {
        bsp_wifi_msg_t msg;
        unsigned int sz = sizeof(msg);
        if (osal_msg_queue_read_copy(s_msg_queue, &msg, &sz, OSAL_WAIT_FOREVER) != OSAL_SUCCESS)
            continue;

        switch (msg.id) {
            case WIFI_MSG_START: {
                s_sta_from_portal = msg.from_portal;
                s_user_initiated = msg.user_initiated;
                sta_task_start();
                char ssid[32] = {0}, pwd[64] = {0};
                storage_service_get_wifi_config(ssid, pwd);
                if (ssid[0] == '\0') {
                    printf("[WiFi Mgr] 无配置，启动 AP\r\n");
                    ap_task_start();
                } else {
                    printf("[WiFi Mgr] 配置 SSID=%s，唤醒 STA (来源:%s)\r\n",
                           ssid, msg.from_portal ? "Portal" : "UDP/启动");
                    if (!msg.from_portal)
                        ap_task_stop();
                    bsp_wifi_set_current_config(ssid, pwd);
                    bsp_wifi_sta_wakeup();
                }
                break;
            }
            case WIFI_MSG_STA_GOT_IP:
                printf("[WiFi Mgr] STA 已连接，关闭 AP\r\n");
                ap_task_stop();
                break;
            case WIFI_MSG_STA_FAIL:
                if (s_sta_from_portal) {
                    printf("[WiFi Mgr] STA 失败(Portal)，AP 保持运行\r\n");
                    captive_portal_service_notify_sta_fail();
                } else if (s_user_initiated) {
                    if (s_prev_ssid[0] != '\0') {
                        printf("[WiFi Mgr] STA 失败，恢复旧网络: %s\r\n", s_prev_ssid);
                        storage_service_save_wifi_config(s_prev_ssid, s_prev_pwd);
                        bsp_wifi_set_current_config(s_prev_ssid, s_prev_pwd);
                        bsp_wifi_sta_wakeup();
                    } else {
                        printf("[WiFi Mgr] STA 失败，无旧网络，启动 AP\r\n");
                        ap_task_start();
                    }
                } else {
                    printf("[WiFi Mgr] STA 失败(启动)，启动 AP\r\n");
                    ap_task_start();
                }
                break;
            case WIFI_MSG_STA_STOPPED:
                printf("[WiFi Mgr] STA 任务完全退出\r\n");
                break;
            case WIFI_MSG_AP_READY:
                captive_portal_service_notify_ap_ready();
                break;
            case WIFI_MSG_AP_STOPPED:
                captive_portal_service_notify_ap_stopped();
                break;
            default:
                break;
        }
    }
    return 0;
}

int bsp_wifi_mgr_init(void)
{
    if (s_mgr_task) return 0;
    if (osal_msg_queue_create("wifi_mgr", 8, &s_msg_queue, 0, sizeof(bsp_wifi_msg_t)) != OSAL_SUCCESS)
        return -1;
    s_mgr_task = robot_task_create_locked("wifi_mgr", (osal_kthread_handler)mgr_task_main,
                                          NULL, MGR_STACK_SIZE, MGR_PRIO);
    return s_mgr_task ? 0 : -1;
}

int bsp_wifi_mgr_send_msg(const bsp_wifi_msg_t *msg)
{
    if (s_msg_queue == 0) return -1;
    return (osal_msgq_overwrite(s_msg_queue, 8, msg, sizeof(*msg)) == OSAL_SUCCESS) ? 0 : -1;
}

int bsp_wifi_connect_ap(const char *ssid, const char *password)
{
    // 保存旧配置，用于失败后恢复
    storage_service_get_wifi_config(s_prev_ssid, s_prev_pwd);
    if (ssid && password) storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START, .from_portal = false, .user_initiated = true});
}

int bsp_wifi_connect_ap_from_portal(const char *ssid, const char *password)
{
    if (ssid && password) storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START, .from_portal = true});
}


