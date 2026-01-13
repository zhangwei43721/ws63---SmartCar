/**
 ****************************************************************************************************
 * @file        wifi_example.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
 * @brief       WiFi连接示例（参考tcp_client示例）
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：连接到WiFi并获取IP地址
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include "../../drivers/wifi_client/bsp_wifi.h"

#define WIFI_TASK_STACK_SIZE 0x2000

// WiFi配置
#define EXAMPLE_WIFI_SSID "BS-8"
#define EXAMPLE_WIFI_PASSWORD "BS88888888"

/**
 * @brief WiFi测试任务
 */
static void wifi_task_entry(const char *arg)
{
    UNUSED(arg);
    int ret;
    char ip_str[32] = {0};

    printf("[WiFi Example] Start\r\n");

    // 初始化WiFi
    ret = bsp_wifi_init();
    if (ret != 0) {
        printf("[WiFi Example] Failed to initialize WiFi\r\n");
        return;
    }

    // 连接到AP
    ret = bsp_wifi_connect_ap(EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASSWORD);
    if (ret != 0) {
        printf("[WiFi Example] Failed to connect to AP\r\n");
        return;
    }

    // 获取IP地址
    ret = bsp_wifi_get_ip(ip_str, sizeof(ip_str));
    if (ret == 0) {
        printf("[WiFi Example] IP address = %s\r\n", ip_str);
    }

    printf("[WiFi Example] WiFi connection test completed\r\n");

    // 持续运行
    while (1) {
        osDelay(1000);
    }
}

/**
 * @brief WiFi示例入口
 */
static void wifi_example_entry(void)
{
    osThreadAttr_t attr;

    printf("[WiFi Example] Entry\r\n");

    attr.name = "wifi_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = WIFI_TASK_STACK_SIZE;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)wifi_task_entry, NULL, &attr) == NULL) {
        printf("[WiFi Example] Failed to create task\r\n");
    } else {
        printf("[WiFi Example] Task created successfully\r\n");
    }
}

/* Run the wifi_example_entry. */
app_run(wifi_example_entry);
