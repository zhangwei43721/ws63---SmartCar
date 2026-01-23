/**
 ****************************************************************************************************
 * @file        bsp_wifi.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
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
#include "wifi_device.h"
#include "wifi_event.h"
#include "lwip/netifapi.h"
#include "lwip/netif.h"
#include "soc_osal.h"
#include "cmsis_os2.h"
#include "td_type.h"
#include "std_def.h"
#include <string.h>
#include <stdio.h>

#define WIFI_SCAN_AP_LIMIT 64
#define WIFI_CONN_STATUS_MAX_GET_TIMES 5
#define DHCP_BOUND_STATUS_MAX_GET_TIMES 20
#define WIFI_STA_IP_MAX_GET_TIMES 5

static wifi_event_stru g_wifi_event_cb = {0};                  /* WiFi 事件回调结构体 */
static bsp_wifi_status_t g_wifi_status = BSP_WIFI_STATUS_IDLE; /* WiFi 当前连接状态 */

/**
 * @brief WiFi连接状态变化回调
 */
static void wifi_connection_changed(td_s32 state, const wifi_linked_info_stru *info, td_s32 reason_code)
{
    UNUSED(reason_code);

    if (state == WIFI_STATE_AVALIABLE) {
        printf("[WiFi] Connected: %s, RSSI: %d\r\n", info->ssid, info->rssi);
        g_wifi_status = BSP_WIFI_STATUS_CONNECTED;
    } else if (state == WIFI_STATE_NOT_AVALIABLE) {
        printf("[WiFi] Disconnected\r\n");
        g_wifi_status = BSP_WIFI_STATUS_DISCONNECTED;
    }
}

/**
 * @brief WiFi扫描状态变化回调
 */
static void wifi_scan_state_changed(td_s32 state, td_s32 size)
{
    UNUSED(state);
    UNUSED(size);
    printf("[WiFi] Scan done!\r\n");
}

/**
 * @brief 获取匹配的网络
 */
static errcode_t get_match_network(const char *expected_ssid, const char *key, wifi_sta_config_stru *expected_bss)
{
    uint32_t num = WIFI_SCAN_AP_LIMIT;
    uint32_t bss_index = 0;
    uint32_t scan_len = sizeof(wifi_scan_info_stru) * WIFI_SCAN_AP_LIMIT;
    wifi_scan_info_stru *result = osal_kmalloc(scan_len, OSAL_GFP_ATOMIC);

    if (result == NULL) {
        return ERRCODE_MALLOC;
    }

    memset_s(result, scan_len, 0, scan_len);
    if (wifi_sta_get_scan_info(result, &num) != ERRCODE_SUCC) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }

    /* 筛选扫描到的Wi-Fi网络 */
    for (bss_index = 0; bss_index < num; bss_index++) {
        if (strlen(expected_ssid) == strlen(result[bss_index].ssid)) {
            if (memcmp(expected_ssid, result[bss_index].ssid, strlen(expected_ssid)) == 0) {
                break;
            }
        }
    }

    if (bss_index >= num) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }

    /* 复制网络信息 */
    if (memcpy_s(expected_bss->ssid, WIFI_MAX_SSID_LEN, result[bss_index].ssid, WIFI_MAX_SSID_LEN) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    if (memcpy_s(expected_bss->bssid, WIFI_MAC_LEN, result[bss_index].bssid, WIFI_MAC_LEN) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    expected_bss->security_type = result[bss_index].security_type;
    if (memcpy_s(expected_bss->pre_shared_key, WIFI_MAX_KEY_LEN, key, strlen(key)) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    expected_bss->ip_type = DHCP;
    osal_kfree(result);
    return ERRCODE_SUCC;
}

/**
 * @brief 初始化WiFi
 */
int bsp_wifi_init(void)
{
    printf("[BSP WiFi] Initializing...\r\n");

    /* 注册WiFi事件回调 */
    g_wifi_event_cb.wifi_event_scan_state_changed = wifi_scan_state_changed;
    g_wifi_event_cb.wifi_event_connection_changed = wifi_connection_changed;

    if (wifi_register_event_cb(&g_wifi_event_cb) != 0) {
        printf("[BSP WiFi] Failed to register event callback\r\n");
        return -1;
    }

    /* 等待WiFi初始化完成 */
    while (wifi_is_wifi_inited() == 0) {
        osDelay(10);
    }

    /* 使能STA模式 */
    if (wifi_sta_enable() != ERRCODE_SUCC) {
        printf("[BSP WiFi] Failed to enable STA mode\r\n");
        return -1;
    }

    g_wifi_status = BSP_WIFI_STATUS_IDLE;
    printf("[BSP WiFi] Initialized successfully\r\n");

    return 0;
}

/**
 * @brief 连接到指定的AP
 */
int bsp_wifi_connect_ap(const char *ssid, const char *password)
{
    char ifname[WIFI_IFNAME_MAX_SIZE + 1] = "wlan0";
    wifi_sta_config_stru expected_bss = {0};
    struct netif *netif_p = NULL;
    wifi_linked_info_stru wifi_status;
    uint8_t index = 0;
    errcode_t ret;

    if (ssid == NULL || password == NULL) {
        printf("[BSP WiFi] Invalid parameters\r\n");
        return -1;
    }

    printf("[BSP WiFi] Connecting to %s...\r\n", ssid);
    g_wifi_status = BSP_WIFI_STATUS_CONNECTING;

    /* 扫描并连接 */
    do {
        printf("[BSP WiFi] Start scan...\r\n");
        osDelay(100);

        if (wifi_sta_scan() != ERRCODE_SUCC) {
            printf("[BSP WiFi] Scan failed, retry...\r\n");
            continue;
        }

        osDelay(300);

        ret = get_match_network(ssid, password, &expected_bss);
        if (ret != ERRCODE_SUCC) {
            printf("[BSP WiFi] Cannot find AP, retry...\r\n");
            continue;
        }

        printf("[BSP WiFi] Connecting...\r\n");
        if (wifi_sta_connect(&expected_bss) != ERRCODE_SUCC) {
            continue;
        }

        /* 检查连接状态 */
        for (index = 0; index < WIFI_CONN_STATUS_MAX_GET_TIMES; index++) {
            osDelay(50);
            memset_s(&wifi_status, sizeof(wifi_linked_info_stru), 0, sizeof(wifi_linked_info_stru));
            if (wifi_sta_get_ap_info(&wifi_status) != ERRCODE_SUCC) {
                continue;
            }
            if (wifi_status.conn_state == WIFI_CONNECTED) {
                break;
            }
        }

        if (wifi_status.conn_state == WIFI_CONNECTED) {
            break;
        }
    } while (1);

    /* DHCP获取IP */
    netif_p = netifapi_netif_find(ifname);
    if (netif_p == NULL) {
        printf("[BSP WiFi] netif_find failed\r\n");
        return -1;
    }

    if (netifapi_dhcp_start(netif_p) != ERR_OK) {
        printf("[BSP WiFi] DHCP start failed\r\n");
        return -1;
    }

    for (uint8_t i = 0; i < DHCP_BOUND_STATUS_MAX_GET_TIMES; i++) {
        osDelay(50);
        if (netifapi_dhcp_is_bound(netif_p) == ERR_OK) {
            printf("[BSP WiFi] DHCP bound success\r\n");
            break;
        }
    }

    for (uint8_t i = 0; i < WIFI_STA_IP_MAX_GET_TIMES; i++) {
        osDelay(1);
        if (netif_p->ip_addr.u_addr.ip4.addr != 0) {
            printf("[BSP WiFi] IP: %u.%u.%u.%u\r\n", (netif_p->ip_addr.u_addr.ip4.addr & 0x000000ff),
                   (netif_p->ip_addr.u_addr.ip4.addr & 0x0000ff00) >> 8,
                   (netif_p->ip_addr.u_addr.ip4.addr & 0x00ff0000) >> 16,
                   (netif_p->ip_addr.u_addr.ip4.addr & 0xff000000) >> 24);

            g_wifi_status = BSP_WIFI_STATUS_GOT_IP;
            printf("[BSP WiFi] Connected successfully\r\n");
            return 0;
        }
    }

    printf("[BSP WiFi] Failed to get IP\r\n");
    return -1;
}

/**
 * @brief 断开WiFi连接
 */
int bsp_wifi_disconnect(void)
{
    errcode_t ret;

    ret = wifi_sta_disconnect();
    if (ret != ERRCODE_SUCC) {
        printf("[BSP WiFi] Failed to disconnect\r\n");
        return -1;
    }

    g_wifi_status = BSP_WIFI_STATUS_IDLE;
    printf("[BSP WiFi] Disconnected\r\n");

    return 0;
}

/**
 * @brief 获取WiFi连接状态
 */
bsp_wifi_status_t bsp_wifi_get_status(void)
{
    return g_wifi_status;
}

/**
 * @brief 获取本机IP地址
 */
int bsp_wifi_get_ip(char *ip_str, uint32_t len)
{
    struct netif *netif_p = netifapi_netif_find("wlan0");

    if (netif_p == NULL || ip_str == NULL || len == 0) {
        return -1;
    }

    if (netif_p->ip_addr.u_addr.ip4.addr == 0) {
        strncpy_s(ip_str, len, "0.0.0.0", strlen("0.0.0.0"));
        return -1;
    }

    snprintf(ip_str, len, "%u.%u.%u.%u", (netif_p->ip_addr.u_addr.ip4.addr & 0x000000ff),
             (netif_p->ip_addr.u_addr.ip4.addr & 0x0000ff00) >> 8,
             (netif_p->ip_addr.u_addr.ip4.addr & 0x00ff0000) >> 16,
             (netif_p->ip_addr.u_addr.ip4.addr & 0xff000000) >> 24);

    return 0;
}

/**
 * @brief 注册WiFi事件回调（已废弃，事件在初始化时注册）
 */
int bsp_wifi_register_event_handler(void *handler)
{
    UNUSED(handler);
    return 0;
}

/**
 * @brief 使用默认配置连接WiFi
 */
int bsp_wifi_connect_default(void)
{
    return bsp_wifi_connect_ap(BSP_WIFI_SSID, BSP_WIFI_PASSWORD);
}
