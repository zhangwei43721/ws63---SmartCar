/**
 ****************************************************************************************************
 * @file        bsp_wifi.h
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
 * @brief       WiFi连接BSP层头文件（参考tcp_client示例）
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

// WiFi工作模式
typedef enum {
  BSP_WIFI_MODE_STA = 0,  // STA模式：连接路由器
  BSP_WIFI_MODE_AP = 1,   // AP模式：作为热点
} bsp_wifi_mode_t;

// AP模式配置（作为热点）
#define BSP_WIFI_AP_SSID "WS63_Robot"
#define BSP_WIFI_AP_PASSWORD ""
#define BSP_WIFI_AP_CHANNEL 13

// WiFi连接状态
typedef enum {
  BSP_WIFI_STATUS_IDLE = 0,     /* 空闲：未开始连接 */
  BSP_WIFI_STATUS_CONNECTING,   /* 连接中：正在尝试连接AP */
  BSP_WIFI_STATUS_CONNECTED,    /* 已连接：已关联到AP，但未获取IP */
  BSP_WIFI_STATUS_GOT_IP,       /* 已获取IP：连接成功，可进行网络通信 */
  BSP_WIFI_STATUS_DISCONNECTED, /* 已断开：连接中断或断开 */
} bsp_wifi_status_t;

/**
 * @brief 连接到指定的AP
 * @param ssid SSID
 * @param password 密码
 * @return 0成功，-1失败
 */
int bsp_wifi_connect_ap(const char* ssid, const char* password);

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
int bsp_wifi_get_ip(char* ip_str, uint32_t len);

/**
 * @brief 获取当前WiFi工作模式
 * @return WiFi工作模式 (STA/AP)
 */
bsp_wifi_mode_t bsp_wifi_get_mode(void);

/**
 * @brief 启动STA模式并尝试连接（带超时）
 * @param ssid SSID
 * @param password 密码
 * @param timeout_ms 超时时间（毫秒）
 * @return 0成功，-1失败
 */
int bsp_wifi_start_sta_with_timeout(const char* ssid, const char* password,
                                    uint32_t timeout_ms);

/**
 * @brief 智能初始化WiFi（自动选择模式）
 * @return 0成功，-1失败
 * @note 先尝试连接预设WiFi，失败后自动切换到AP模式
 */
int bsp_wifi_smart_init(void);

/**
 * @brief 从AP模式切换到STA模式
 * @param ssid 目标SSID
 * @param password 目标密码
 * @return 0成功，-1失败
 */
int bsp_wifi_switch_from_ap_to_sta(const char* ssid, const char* password);

/**
 * @brief 从STA模式切换到AP模式
 * @return 0成功，-1失败
 * @note 断开当前STA连接，启动AP热点
 */
int bsp_wifi_switch_to_ap(void);

/**
 * @brief WiFi 扫描结果条目（精简版，用于上报到前端）
 */
typedef struct {
  char ssid[33];        /* SSID 字符串（最大 32 字节 + \0） */
  int8_t rssi;          /* 信号强度（dBm） */
  uint8_t security;     /* 加密类型 */
  uint8_t channel;      /* 信道 */
} bsp_wifi_scan_item_t;

/**
 * @brief 扫描周边 WiFi 并填充结果列表
 * @param items 输出数组
 * @param max_count 最多写入条数
 * @param out_count 实际写入条数
 * @return 0 成功，-1 失败
 */
int bsp_wifi_scan_list(bsp_wifi_scan_item_t* items, uint32_t max_count,
                       uint32_t* out_count);

#endif /* __BSP_WIFI_H__ */
