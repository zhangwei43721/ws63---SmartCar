#ifndef WIFI_MGR_SERVICE_H
#define WIFI_MGR_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

// ========== 消息（WiFi event → Manager 消息队列）==========
typedef enum {
    WIFI_MSG_START = 0,       // 启动：根据 NV 配置决定 STA 或 AP
    WIFI_MSG_STA_GOT_IP,      // STA 拿到 IP，连接成功
    WIFI_MSG_STA_FAIL,        // STA 扫描/连接失败
    WIFI_MSG_STA_STOPPED,     // STA 任务已停止
    WIFI_MSG_AP_READY,        // AP 就绪（DHCP 服务启动完成）
    WIFI_MSG_AP_STOPPED,      // AP 任务已停止
} wifi_msg_id_t;

// Manager 消息队列单元
typedef struct {
    wifi_msg_id_t id; // 消息类型
} bsp_wifi_msg_t;

// ========== Manager ==========
int bsp_wifi_mgr_init(void);
int bsp_wifi_mgr_send_msg(const bsp_wifi_msg_t *msg);

// ========== 向后兼容接口 ==========
int bsp_wifi_smart_init(void);
int bsp_wifi_connect_ap(const char *ssid, const char *password);
int bsp_wifi_switch_to_ap(void);

#endif
int robot_wifi_apply_config(const char *ssid, const char *password);
