/**
 ****************************************************************************************************
 * @file        bsp_wifi.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       WiFi连接BSP层头文件
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 */

#ifndef __BSP_WIFI_H__
#define __BSP_WIFI_H__

#include <stdint.h>
#include "errcode.h"

// WiFi配置（可在代码中修改）
#define BSP_WIFI_SSID     "YourWiFiSSID"
#define BSP_WIFI_PASSWORD "YourWiFiPassword"

// WiFi安全类型
typedef enum {
    BSP_WIFI_SEC_OPEN = 0,     // 开放网络
    BSP_WIFI_SEC_WPA_PSK,      // WPA-PSK
    BSP_WIFI_SEC_WPA2_PSK,     // WPA2-PSK
} bsp_wifi_security_t;

// WiFi事件类型
typedef enum {
    BSP_WIFI_EVENT_STA_CONNECT = 0,     // 站已连接
    BSP_WIFI_EVENT_STA_DISCONNECT,      // 站已断开
    BSP_WIFI_EVENT_STA_GOT_IP,          // 已获取IP
} bsp_wifi_event_t;

// WiFi连接状态
typedef enum {
    BSP_WIFI_STATUS_IDLE = 0,      // 空闲
    BSP_WIFI_STATUS_CONNECTING,    // 连接中
    BSP_WIFI_STATUS_CONNECTED,     // 已连接
    BSP_WIFI_STATUS_GOT_IP,        // 已获取IP
    BSP_WIFI_STATUS_DISCONNECTED,  // 已断开
} bsp_wifi_status_t;

/**
 * @brief WiFi事件回调函数类型
 * @param event WiFi事件
 * @param data 事件数据
 */
typedef void (*bsp_wifi_event_handler_t)(bsp_wifi_event_t event, void *data);

/**
 * @brief 初始化WiFi
 * @return 0成功，-1失败
 */
int bsp_wifi_init(void);

/**
 * @brief 连接到指定的AP
 * @param ssid SSID
 * @param password 密码
 * @return 0成功，-1失败
 */
int bsp_wifi_connect_ap(const char *ssid, const char *password);

/**
 * @brief 断开WiFi连接
 * @return 0成功，-1失败
 */
int bsp_wifi_disconnect(void);

/**
 * @brief 获取WiFi连接状态
 * @return WiFi状态
 */
bsp_wifi_status_t bsp_wifi_get_status(void);

/**
 * @brief 获取本机IP地址
 * @param ip_str IP地址字符串缓冲区
 * @param len 缓冲区长度
 * @return 0成功，-1失败
 */
int bsp_wifi_get_ip(char *ip_str, uint32_t len);

/**
 * @brief 注册WiFi事件回调
 * @param handler 事件处理函数
 * @return 0成功，-1失败
 */
int bsp_wifi_register_event_handler(bsp_wifi_event_handler_t handler);

/**
 * @brief 使用默认配置连接WiFi
 * @return 0成功，-1失败
 */
int bsp_wifi_connect_default(void);

#endif /* __BSP_WIFI_H__ */
