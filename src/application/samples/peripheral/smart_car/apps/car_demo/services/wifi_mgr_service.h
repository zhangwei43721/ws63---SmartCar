#ifndef WIFI_MGR_SERVICE_H
#define WIFI_MGR_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../drivers/wifi_client/bsp_wifi_sta.h"

// WiFi 管理器（服务层）：STA/AP 生命周期与配网策略的唯一拥有者。
// 注意：本模块是 service 而非 BSP 驱动，接口统一以 wifi_mgr_ 为前缀。

// ========== 消息（WiFi event → Manager 消息队列）==========
// 枚举值与 bsp_wifi_event_t 1:1 对齐，可直接强转
typedef enum {
    WIFI_MSG_STA_GOT_IP = 0, // STA 拿到 IP，连接成功
    WIFI_MSG_STA_FAIL,       // STA 扫描/连接失败
    WIFI_MSG_STA_STOPPED,    // STA 任务已停止
    WIFI_MSG_AP_READY,       // AP 就绪（DHCP 服务启动完成）
    WIFI_MSG_AP_STOPPED,     // AP 任务已停止
    WIFI_MSG_COUNT,          // 状态总数（勿作状态用）
    WIFI_MSG_START = 0xFF,   // 初始态：尚未连接任何网络
} wifi_status_t;

// Manager 消息队列单元
typedef struct {
    wifi_status_t id;    // 消息类型
    bool from_portal;    // 配网来源：true=HTTP portal，false=其他
    bool user_initiated; // true=用户UDP手动配网，false=系统启动自动连接
} wifi_mgr_msg_t;

// ========== Manager ==========
int wifi_mgr_init(void);                          // 初始化 WiFi 管理器（创建任务 + 消息队列）
int wifi_mgr_send_msg(const wifi_mgr_msg_t *msg); // 向 WiFi 管理器投递消息

// ========== WiFi 状态查询 & 订阅 ==========
extern volatile wifi_status_t g_wifi_status; // WiFi 当前状态，WIFI_MSG_START=未就绪（一次性查询用，禁止轮询）
typedef void (*wifi_state_cb_t)(bsp_wifi_event_t event, const char *ip);

// 订阅 WiFi 状态变化（追加到订阅表，回调运行在 wifi_mgr 任务上下文，应只做投队列等轻量操作）
// @return 0 成功；-1 订阅表已满（显式失败，杜绝旧版单指针的静默覆盖问题）
int wifi_mgr_subscribe(wifi_state_cb_t cb);

const char *wifi_mgr_get_ip(void);           // 获取当前 IP 地址字符串

// ========== 配网接口 ==========
int wifi_mgr_connect_ap(const char *ssid, const char *password); // 请求连接到指定 AP（STA 模式）
int wifi_mgr_connect_ap_from_portal(const char *ssid,
                                    const char *password); // 从配网页面请求连接 AP（带 portal 标记）

// 处理 WiFi 配置包（0xE0 保存 / 0xE1 立即连接，变长格式）。配网是 WiFi 管理器的权责，
// 通道（UDP 等）只做识别与转发，应答包由通道自行发出。
// @return true 包已消费；*ack 输出应答码（0x00 成功 / 0x01 失败），0xFF 表示无需应答
bool wifi_mgr_handle_config_packet(const uint8_t *data, size_t len, uint8_t *ack);

#endif
