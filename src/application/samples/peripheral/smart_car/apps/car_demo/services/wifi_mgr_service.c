/**
 * @file wifi_mgr_service.c
 * @brief WiFi Manager：STA 挂起待命 + AP 按需精简版
 */

#include "wifi_mgr_service.h"
#include <stdio.h>
#include <string.h>
#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "../../../drivers/wifi_client/bsp_wifi_ap.h"
#include "../car_common.h"
#include "securec.h"
#include "soc_osal.h"
#include "../../../platform/storage_service.h"
#include "captive_portal_service.h"
#include "ui_service.h"

static osal_task *s_mgr_task = NULL;   // WiFi 管理任务句柄
static unsigned long s_msg_queue = 0;  // WiFi 管理消息队列 ID
static bool s_sta_from_portal = false; // 本次 STA 连接是否来自 Portal 配网
static bool s_user_initiated = false;  // 是否为用户主动发起的连接
static char s_prev_ssid[32] = {0};     // 上次连接的 SSID，用于重连判断
static char s_prev_pwd[64] = {0};      // 上次连接的密码，用于重连判断

// WiFi 就绪状态：存触发就绪的事件 ID，WIFI_MSG_START=未就绪
volatile wifi_status_t g_wifi_status = WIFI_MSG_START;
static wifi_state_cb_t s_state_cb = NULL;

// WiFi事件回调
static void wifi_event_cb(bsp_wifi_event_t event, void *arg)
{
    (void)arg;
    if (event > BSP_WIFI_EVENT_AP_STOPPED)
        return;
    bsp_wifi_msg_t msg = {.id = (wifi_status_t)event};
    (void)bsp_wifi_mgr_send_msg(&msg);
}

// 通知外部：通过函数指针 s_state_cb 调用已注册的回调
// 当前唯一注册者：udp_service 的 on_wifi_state_change（在 udp_service_init 中注册）
static void notify_wifi_change(bsp_wifi_event_t event, const char *ip)
{
    if (s_state_cb)
        s_state_cb(event, ip); // 等价于调用 on_wifi_state_change(event, ip)
}

// WiFi管理主任务：从消息队列接收事件并驱动STA/AP状态切换
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
            case WIFI_MSG_START: { // 启动连接：有配置走 STA，无配置走 AP
                s_sta_from_portal = msg.from_portal;
                s_user_initiated = msg.user_initiated;
                sta_task_start();
                char ssid[32] = {0}, pwd[64] = {0};
                storage_service_get_wifi_config(ssid, pwd);
                if (ssid[0] == '\0') {
                    printf("[WiFi Mgr] 无配置，启动 AP\r\n");
                    ap_task_start();
                } else {
                    printf("[WiFi Mgr] 配置 SSID=%s，唤醒 STA (来源:%s)\r\n", ssid,
                           msg.from_portal ? "Portal" : "UDP/启动");
                    bsp_wifi_set_current_config(ssid, pwd);
                    bsp_wifi_sta_wakeup();
                }
                break;
            }
            case WIFI_MSG_STA_GOT_IP: { // STA 拿到 IP → 关 AP，标记就绪
                printf("[WiFi Mgr] STA 已连接，关闭 AP\r\n");
                ap_task_stop();
                g_wifi_status = WIFI_MSG_STA_GOT_IP;
                char ip[32] = {0};
                bsp_wifi_get_ip(ip, sizeof(ip));
                ui_show_mode_page(CAR_STOP_STATUS);
                notify_wifi_change(BSP_WIFI_EVENT_STA_GOT_IP, ip);
                break;
            }
            case WIFI_MSG_STA_FAIL: // STA 连接失败 → 按来源走不同恢复策略
                g_wifi_status = WIFI_MSG_START;
                notify_wifi_change(BSP_WIFI_EVENT_STA_FAIL, NULL);
                if (s_sta_from_portal) {
                    printf("[WiFi Mgr] STA 失败(Portal)，AP 保持运行\r\n");
                    captive_portal_service_notify_sta_fail();
                } else if (s_user_initiated) {
                    if (s_prev_ssid[0] != '\0') {
                        printf("[WiFi Mgr] STA 失败，恢复旧网络: %s\r\n", s_prev_ssid);
                        storage_service_save_wifi_config(s_prev_ssid, s_prev_pwd);
                        bsp_wifi_set_current_config(s_prev_ssid, s_prev_pwd);
                        s_prev_ssid[0] = '\0'; // 只尝试一次，避免死循环
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
            case WIFI_MSG_STA_STOPPED: // STA 任务完全退出（驱动已关闭）
                g_wifi_status = WIFI_MSG_START;
                printf("[WiFi Mgr] STA 任务完全退出\r\n");
                notify_wifi_change(BSP_WIFI_EVENT_STA_STOPPED, NULL);
                break;
            case WIFI_MSG_AP_READY: { // AP 热点就绪（DHCP 服务已启动）
                g_wifi_status = WIFI_MSG_AP_READY;
                char ip[32] = {0};
                bsp_wifi_get_ip(ip, sizeof(ip));
                ui_show_mode_page(CAR_STOP_STATUS);
                notify_wifi_change(BSP_WIFI_EVENT_AP_READY, ip);
                captive_portal_service_notify_ap_ready();
                break;
            }
            case WIFI_MSG_AP_STOPPED: // AP 热点已关闭
                if (g_wifi_status == WIFI_MSG_AP_READY)
                    g_wifi_status = WIFI_MSG_START;
                notify_wifi_change(BSP_WIFI_EVENT_AP_STOPPED, NULL);
                captive_portal_service_notify_ap_stopped();
                break;
            default:
                break;
        }
    }
    return 0;
}

// 注册 WiFi 状态变化回调（保存函数指针，WiFi 事件发生时调用）
void bsp_wifi_mgr_register_cb(wifi_state_cb_t cb)
{
    s_state_cb = cb; // 存函数地址，之后 notify_wifi_change() 通过它回调
}

// 获取当前 WiFi 的 IP 地址字符串（UI 服务调用）
const char *bsp_wifi_mgr_get_ip(void)
{
    static char ip_buf[32] = "0.0.0.0";
    bsp_wifi_get_ip(ip_buf, sizeof(ip_buf));
    return ip_buf;
}

// 初始化WiFi管理器：创建消息队列和管理任务
int bsp_wifi_mgr_init(void)
{
    if (s_mgr_task)
        return 0;
    if (osal_msg_queue_create("wifi_mgr", 8, &s_msg_queue, 0, sizeof(bsp_wifi_msg_t)) != OSAL_SUCCESS)
        return -1;
    s_mgr_task = car_task_create_locked("wifi_mgr", (osal_kthread_handler)mgr_task_main, NULL, 2048, 20);
    return s_mgr_task ? 0 : -1;
}

// 向WiFi管理任务发送消息（覆盖式写入队列）
int bsp_wifi_mgr_send_msg(const bsp_wifi_msg_t *msg)
{
    if (s_msg_queue == 0)
        return -1;
    return (osal_msgq_overwrite(s_msg_queue, 8, msg, sizeof(*msg)) == OSAL_SUCCESS) ? 0 : -1;
}

// 发起STA连接指定AP（保存旧配置用于失败恢复）
int bsp_wifi_connect_ap(const char *ssid, const char *password)
{
    // 保存旧配置，用于失败后恢复
    storage_service_get_wifi_config(s_prev_ssid, s_prev_pwd);
    if (ssid && password)
        storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START, .from_portal = false, .user_initiated = true});
}

// 从Portal页面发起STA连接（失败后保持AP模式）
int bsp_wifi_connect_ap_from_portal(const char *ssid, const char *password)
{
    if (ssid && password)
        storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return bsp_wifi_mgr_send_msg(&(bsp_wifi_msg_t){.id = WIFI_MSG_START, .from_portal = true});
}
