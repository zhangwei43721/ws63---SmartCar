/**
 ****************************************************************************************************
 * @file        bsp_wifi.c
 * @author      Smart Car Team
 * @version     V1.0
 * @date        2025-01-12
 * @brief       WiFi连接BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 */

#include "bsp_wifi.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"
#include "lwip/netifapi.h"
#include "soc_osal.h"
#include <string.h>
#include <stdio.h>

#define WIFI_SCAN_AP_LIMIT 64
#define WIFI_CONN_STATUS_MAX_GET_TIMES 5
#define DHCP_BOUND_STATUS_MAX_GET_TIMES 20
#define WIFI_STA_IP_MAX_GET_TIMES 5

static bsp_wifi_event_handler_t g_wifi_event_handler = NULL;
static bsp_wifi_status_t g_wifi_status = BSP_WIFI_STATUS_IDLE;

/**
 * @brief 触发WiFi事件回调
 * @param event WiFi事件
 * @param data 事件数据
 */
static void bsp_wifi_trigger_event(bsp_wifi_event_t event, void *data)
{
    if (g_wifi_event_handler != NULL) {
        g_wifi_event_handler(event, data);
    }
}

/**
 * @brief 初始化WiFi
 * @return 0成功，-1失败
 */
int bsp_wifi_init(void)
{
    errcode_t ret;

    printf("BSP WiFi: Initializing...\n");

    // 使能STA模式
    ret = wifi_sta_enable();
    if (ret != ERRCODE_SUCC) {
        printf("BSP WiFi: Failed to enable STA mode\n");
        return -1;
    }

    g_wifi_status = BSP_WIFI_STATUS_IDLE;
    printf("BSP WiFi: Initialized successfully\n");

    return 0;
}

/**
 * @brief 连接到指定的AP
 * @param ssid SSID
 * @param password 密码
 * @return 0成功，-1失败
 */
int bsp_wifi_connect_ap(const char *ssid, const char *password)
{
    errcode_t ret;
    wifi_sta_config_stru *sta_config = NULL;
    uint8_t scan_times = 0;

    if (ssid == NULL || password == NULL) {
        printf("BSP WiFi: Invalid parameters\n");
        return -1;
    }

    printf("BSP WiFi: Connecting to %s...\n", ssid);

    g_wifi_status = BSP_WIFI_STATUS_CONNECTING;

    // 扫描并连接到指定的AP
    // 这里简化处理，实际应用中需要更完整的扫描和连接逻辑
    sta_config = (wifi_sta_config_stru *)osal_kmalloc(sizeof(wifi_sta_config_stru));
    if (sta_config == NULL) {
        printf("BSP WiFi: Failed to allocate memory\n");
        return -1;
    }

    memset_s(sta_config, sizeof(wifi_sta_config_stru), 0, sizeof(wifi_sta_config_stru));
    memcpy_s(sta_config->ssid, WIFI_MAX_SSID_LEN, ssid, strlen(ssid));
    memcpy_s(sta_config->pre_shared_key, WIFI_MAX_KEY_LEN, password, strlen(password));
    sta_config->security_type = WIFI_SEC_TYPE_PSK;

    // 连接到AP
    ret = wifi_sta_connect(sta_config);
    osal_kfree(sta_config);

    if (ret != ERRCODE_SUCC) {
        printf("BSP WiFi: Failed to connect to AP\n");
        g_wifi_status = BSP_WIFI_STATUS_DISCONNECTED;
        return -1;
    }

    printf("BSP WiFi: Connection initiated\n");
    return 0;
}

/**
 * @brief 断开WiFi连接
 * @return 0成功，-1失败
 */
int bsp_wifi_disconnect(void)
{
    errcode_t ret;

    ret = wifi_sta_disconnect();
    if (ret != ERRCODE_SUCC) {
        printf("BSP WiFi: Failed to disconnect\n");
        return -1;
    }

    g_wifi_status = BSP_WIFI_STATUS_IDLE;
    printf("BSP WiFi: Disconnected\n");

    return 0;
}

/**
 * @brief 获取WiFi连接状态
 * @return WiFi状态
 */
bsp_wifi_status_t bsp_wifi_get_status(void)
{
    return g_wifi_status;
}

/**
 * @brief 获取本机IP地址
 * @param ip_str IP地址字符串缓冲区
 * @param len 缓冲区长度
 * @return 0成功，-1失败
 */
int bsp_wifi_get_ip(char *ip_str, uint32_t len)
{
    // 这里需要调用lwip API获取IP地址
    // 简化实现，返回默认值
    if (ip_str == NULL || len == 0) {
        return -1;
    }

    strncpy_s(ip_str, len, "0.0.0.0", strlen("0.0.0.0"));
    return 0;
}

/**
 * @brief 注册WiFi事件回调
 * @param handler 事件处理函数
 * @return 0成功，-1失败
 */
int bsp_wifi_register_event_handler(bsp_wifi_event_handler_t handler)
{
    g_wifi_event_handler = handler;
    return 0;
}

/**
 * @brief 使用默认配置连接WiFi
 * @return 0成功，-1失败
 */
int bsp_wifi_connect_default(void)
{
    return bsp_wifi_connect_ap(BSP_WIFI_SSID, BSP_WIFI_PASSWORD);
}
