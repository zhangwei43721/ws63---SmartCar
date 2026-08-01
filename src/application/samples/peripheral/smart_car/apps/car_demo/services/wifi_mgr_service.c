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

static osal_task *s_mgr_task = NULL;   // WiFi 管理任务句柄
static unsigned long s_msg_queue = 0;  // WiFi 管理消息队列 ID
static bool s_sta_from_portal = false; // 本次 STA 连接是否来自 Portal 配网
static bool s_user_initiated = false;  // 是否为用户主动发起的连接
static char s_prev_ssid[32] = {0};     // 上次连接的 SSID，用于重连判断
static char s_prev_pwd[64] = {0};      // 上次连接的密码，用于重连判断

// WiFi 就绪状态：存触发就绪的事件 ID，WIFI_MSG_START=未就绪
volatile wifi_status_t g_wifi_status = WIFI_MSG_START;

// WiFi 事件订阅表：追加式注册，满了显式失败（旧版单指针赋值会静默覆盖先注册者）
#define WIFI_MGR_MAX_SUBSCRIBERS 4
static wifi_state_cb_t s_subscribers[WIFI_MGR_MAX_SUBSCRIBERS] = {NULL};

// WiFi事件回调
static void wifi_event_cb(bsp_wifi_event_t event, void *arg)
{
    (void)arg;
    if (event > BSP_WIFI_EVENT_AP_STOPPED)
        return;
    wifi_mgr_msg_t msg = {.id = (wifi_status_t)event};
    (void)wifi_mgr_send_msg(&msg);
}

// 广播 WiFi 状态变化给所有订阅者（当前订阅者：udp_channel / captive_portal / ui_service）
static void notify_wifi_change(bsp_wifi_event_t event, const char *ip)
{
    for (unsigned i = 0; i < WIFI_MGR_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[i])
            s_subscribers[i](event, ip);
    }
}

// WiFi管理主任务：从消息队列接收事件并驱动STA/AP状态切换
static int mgr_task_main(void *arg)
{
    (void)arg;
    printf("[WiFi Mgr] 启动\r\n");
    bsp_wifi_register_event_cb(wifi_event_cb, NULL);

    while (1) {
        wifi_mgr_msg_t msg;
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
                notify_wifi_change(BSP_WIFI_EVENT_STA_GOT_IP, ip);
                break;
            }
            case WIFI_MSG_STA_FAIL: // STA 连接失败 → 按来源走不同恢复策略
                g_wifi_status = WIFI_MSG_START;
                notify_wifi_change(BSP_WIFI_EVENT_STA_FAIL, NULL);
                if (s_sta_from_portal) {
                    printf("[WiFi Mgr] STA 失败(Portal)，AP 保持运行\r\n");
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
                notify_wifi_change(BSP_WIFI_EVENT_AP_READY, ip);
                break;
            }
            case WIFI_MSG_AP_STOPPED: // AP 热点已关闭
                if (g_wifi_status == WIFI_MSG_AP_READY)
                    g_wifi_status = WIFI_MSG_START;
                notify_wifi_change(BSP_WIFI_EVENT_AP_STOPPED, NULL);
                break;
            default:
                break;
        }
    }
    return 0;
}

// 订阅 WiFi 状态变化（追加到订阅表空槽位）
int wifi_mgr_subscribe(wifi_state_cb_t cb)
{
    if (cb == NULL)
        return -1;
    for (unsigned i = 0; i < WIFI_MGR_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[i] == NULL) {
            s_subscribers[i] = cb;
            return 0;
        }
        if (s_subscribers[i] == cb) // 重复订阅视为成功
            return 0;
    }
    printf("[WiFi Mgr] 订阅表已满，注册失败！\r\n");
    return -1;
}

// 获取当前 WiFi 的 IP 地址字符串（UI 服务调用）
const char *wifi_mgr_get_ip(void)
{
    static char ip_buf[32] = "0.0.0.0";
    bsp_wifi_get_ip(ip_buf, sizeof(ip_buf));
    return ip_buf;
}

// 初始化WiFi管理器：创建消息队列和管理任务
int wifi_mgr_init(void)
{
    if (s_mgr_task)
        return 0;
    if (osal_msg_queue_create("wifi_mgr", 8, &s_msg_queue, 0, sizeof(wifi_mgr_msg_t)) != OSAL_SUCCESS)
        return -1;
    s_mgr_task = car_task_create_locked("wifi_mgr", (osal_kthread_handler)mgr_task_main, NULL, 2048, 20);
    return s_mgr_task ? 0 : -1;
}

// 向WiFi管理任务发送消息（覆盖式写入队列）
int wifi_mgr_send_msg(const wifi_mgr_msg_t *msg)
{
    if (s_msg_queue == 0)
        return -1;
    return (osal_msgq_overwrite(s_msg_queue, 8, msg, sizeof(*msg)) == OSAL_SUCCESS) ? 0 : -1;
}

// 发起STA连接指定AP（保存旧配置用于失败恢复）
int wifi_mgr_connect_ap(const char *ssid, const char *password)
{
    // 保存旧配置，用于失败后恢复
    storage_service_get_wifi_config(s_prev_ssid, s_prev_pwd);
    if (ssid && password)
        storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return wifi_mgr_send_msg(&(wifi_mgr_msg_t){.id = WIFI_MSG_START, .from_portal = false, .user_initiated = true});
}

// 从Portal页面发起STA连接（失败后保持AP模式）
int wifi_mgr_connect_ap_from_portal(const char *ssid, const char *password)
{
    if (ssid && password)
        storage_service_save_wifi_config(ssid, password);
    bsp_wifi_set_current_config(ssid, password);
    return wifi_mgr_send_msg(&(wifi_mgr_msg_t){.id = WIFI_MSG_START, .from_portal = true});
}

/* ============================================================
 * WiFi 配置包处理（0xE0/0xE1，由 UDP 等通道转发至此）
 * ============================================================ */

// WiFi 配置扩展包（变长，最大 70 字节）
typedef struct {
    uint8_t type;     // 0xE0~0xE1
    uint8_t ssid_len; // SSID 长度（0~32）
    uint8_t pwd_len;  // 密码长度（0~63）
    char payload[64]; // SSID + 密码连续存放
} wifi_config_pkt_t;

// 从WiFi配置包中提取SSID和密码
static void wifi_config_extract_creds(const wifi_config_pkt_t *pkt,
                                      char *ssid,
                                      size_t ssid_sz,
                                      char *pwd,
                                      size_t pwd_sz)
{
    if (pkt->ssid_len > 0 && pkt->ssid_len < ssid_sz) {
        (void)memcpy_s(ssid, ssid_sz, pkt->payload, pkt->ssid_len);
        ssid[pkt->ssid_len] = '\0';
    }
    if (pkt->pwd_len > 0 && pkt->pwd_len < pwd_sz) {
        (void)memcpy_s(pwd, pwd_sz, pkt->payload + pkt->ssid_len, pkt->pwd_len);
        pwd[pkt->pwd_len] = '\0';
    }
}

// 处理 WiFi 配置包：校验 + 提取凭证 + 保存/连接
bool wifi_mgr_handle_config_packet(const uint8_t *data, size_t len, uint8_t *ack)
{
    if (!data || !ack || len < 3)
        return false;
    if (data[0] != CAR_PKT_WIFI_SET && data[0] != CAR_PKT_WIFI_CONNECT)
        return false;

    *ack = 0xFF; // 默认无需应答

    // 公共校验
    const wifi_config_pkt_t *pkt = (const wifi_config_pkt_t *)data;
    if (pkt->ssid_len > 32 || pkt->pwd_len > 63 || (pkt->ssid_len + pkt->pwd_len) > 64)
        return true;
    if (len < (size_t)(3 + pkt->ssid_len + pkt->pwd_len))
        return true;

    char ssid[33] = {0};
    char pwd[64] = {0};
    wifi_config_extract_creds(pkt, ssid, sizeof(ssid), pwd, sizeof(pwd));

    if (data[0] == CAR_PKT_WIFI_CONNECT) { // 0xE1 立即连接
        wifi_mgr_connect_ap(ssid, pwd);
        printf("[WiFi Mgr] 通道请求切换STA: SSID='%s'\r\n", ssid);
    } else { // 0xE0 保存到 NV，需回 ACK
        errcode_t ret = storage_service_save_wifi_config(ssid, pwd);
        *ack = (ret == ERRCODE_SUCC) ? 0x00 : 0x01;
        printf("[WiFi Mgr] WiFi配置保存: SSID='%s' 结果=%d\r\n", ssid, ret);
    }
    return true;
}
