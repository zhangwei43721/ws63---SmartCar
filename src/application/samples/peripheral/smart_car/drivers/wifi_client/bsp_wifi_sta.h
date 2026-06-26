#ifndef BSP_WIFI_STA_H
#define BSP_WIFI_STA_H

#include <stdbool.h>
#include <stdint.h>
#include "soc_osal.h"

// ========== 公共 WiFi 类型 ==========
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

// ========== 状态管理 ==========
bsp_wifi_status_t bsp_wifi_get_status(void);     // 获取当前 WiFi 连接状态
bsp_wifi_mode_t bsp_wifi_get_mode(void);         // 获取当前 WiFi 工作模式（STA/AP）
void bsp_wifi_set_status(bsp_wifi_status_t st);  // 设置 WiFi 连接状态
void bsp_wifi_set_mode(bsp_wifi_mode_t m);       // 设置 WiFi 工作模式
int bsp_wifi_get_ip(char *ip_str, uint32_t len); // 获取当前 IP 地址字符串

// ========== 事件回调 ==========
void bsp_wifi_register_event_cb(bsp_wifi_event_cb_t cb, void *arg); // 注册 WiFi 事件回调函数
void bsp_wifi_notify_event(bsp_wifi_event_t event);                 // 通知所有注册的回调函数

// ========== 扫描接口 ==========
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t security;
    uint8_t channel;
} bsp_wifi_scan_item_t;

int bsp_wifi_scan_list(bsp_wifi_scan_item_t *items, uint32_t max_count, uint32_t *out_count); // 扫描可用 WiFi 列表

// ========== STA Task 接口 ==========
bool sta_task_start(void);      // 启动 WiFi STA 连接任务
void sta_task_stop(void);       // 停止 WiFi STA 连接任务
void bsp_wifi_sta_wakeup(void); // 唤醒 STA 任务（触发重连）

// ========== 当前 WiFi 配置（STA task 读取） ==========
void bsp_wifi_set_current_config(const char *ssid, const char *pwd); // 设置 STA 要连接的 SSID 和密码

// ========== 同步阻塞连接 ==========
int bsp_wifi_connect_sync(const char *ssid, const char *pwd); // 同步阻塞式连接 WiFi（等待连接完成）

#endif
