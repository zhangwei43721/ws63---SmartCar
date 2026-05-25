#ifndef WIFI_MGR_SERVICE_H
#define WIFI_MGR_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

// ========== 消息（Worker -> Manager） ==========
typedef enum {
    WIFI_MSG_START = 0,
    WIFI_MSG_STA_GOT_IP,
    WIFI_MSG_STA_FAIL,
    WIFI_MSG_STA_STOPPED,
    WIFI_MSG_AP_READY,
    WIFI_MSG_AP_STOPPED,
} wifi_msg_id_t;

typedef struct {
    wifi_msg_id_t id;
} bsp_wifi_msg_t;

// ========== Manager ==========
int bsp_wifi_mgr_init(void);
int bsp_wifi_mgr_send_msg(const bsp_wifi_msg_t *msg);

// ========== 向后兼容接口 ==========
int bsp_wifi_smart_init(void);
int bsp_wifi_connect_ap(const char *ssid, const char *password);
int bsp_wifi_switch_to_ap(void);

#endif
