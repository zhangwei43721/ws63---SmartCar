#ifndef BSP_WIFI_STA_H
#define BSP_WIFI_STA_H

#include <stdbool.h>
#include <stdint.h>

/* ========== 公共 WiFi 类型 ========== */
typedef enum {
    BSP_WIFI_MODE_STA = 0,
    BSP_WIFI_MODE_AP = 1,
} bsp_wifi_mode_t;

typedef enum {
    BSP_WIFI_STATUS_IDLE = 0,
    BSP_WIFI_STATUS_CONNECTING,
    BSP_WIFI_STATUS_CONNECTED,
    BSP_WIFI_STATUS_GOT_IP,
    BSP_WIFI_STATUS_DISCONNECTED,
} bsp_wifi_status_t;

typedef enum {
    BSP_WIFI_EVENT_STA_GOT_IP = 0,
    BSP_WIFI_EVENT_STA_FAIL,
    BSP_WIFI_EVENT_STA_STOPPED,
    BSP_WIFI_EVENT_AP_READY,
    BSP_WIFI_EVENT_AP_STOPPED,
} bsp_wifi_event_t;

typedef void (*bsp_wifi_event_cb_t)(bsp_wifi_event_t event, void *arg);

/* ========== 状态管理 ========== */
bsp_wifi_status_t bsp_wifi_get_status(void);
bsp_wifi_mode_t bsp_wifi_get_mode(void);
void bsp_wifi_set_status(bsp_wifi_status_t st);
void bsp_wifi_set_mode(bsp_wifi_mode_t m);
int bsp_wifi_get_ip(char *ip_str, uint32_t len);

/* ========== 事件回调 ========== */
void bsp_wifi_register_event_cb(bsp_wifi_event_cb_t cb, void *arg);
void bsp_wifi_notify_event(bsp_wifi_event_t event);

/* ========== 扫描接口 ========== */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t security;
    uint8_t channel;
} bsp_wifi_scan_item_t;

int bsp_wifi_scan_list(bsp_wifi_scan_item_t *items, uint32_t max_count, uint32_t *out_count);

/* ========== STA Task 接口 ========== */
bool sta_task_start(void);
void sta_task_stop(void);
void sta_set_should_exit(bool v);
bool sta_get_should_exit(void);
void sta_task_wakeup(void);
int sta_task_main(void *arg);

/* ========== 当前 WiFi 配置（STA task 读取） ========== */
void bsp_wifi_set_current_config(const char *ssid, const char *pwd);
void bsp_wifi_get_current_config(char *ssid, uint32_t ssid_len, char *pwd, uint32_t pwd_len);

/* ========== 同步阻塞连接 ========== */
int bsp_wifi_connect_sync(const char *ssid, const char *pwd);

#endif
