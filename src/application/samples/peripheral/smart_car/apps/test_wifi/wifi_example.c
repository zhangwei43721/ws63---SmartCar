/**
 ****************************************************************************************************
 * @file        wifi_example.c
 * @author      Smart Car Team
 * @version     V1.0
 * @date        2025-01-12
 * @brief       WiFi连接示例
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
#include "../../drivers/wifi_client/bsp_wifi.h"

#define WIFI_TASK_STACK_SIZE 0x2000
#define WIFI_TASK_PRIO 24

// 修改为你的WiFi配置
#define EXAMPLE_WIFI_SSID     "YourWiFiSSID"
#define EXAMPLE_WIFI_PASSWORD "YourWiFiPassword"

/**
 * @brief WiFi事件处理回调
 * @param event WiFi事件
 * @param data 事件数据
 */
static void wifi_event_handler(bsp_wifi_event_t event, void *data)
{
    switch (event) {
        case BSP_WIFI_EVENT_STA_CONNECT:
            printf("WiFi Example: Connected to AP\n");
            break;
        case BSP_WIFI_EVENT_STA_DISCONNECT:
            printf("WiFi Example: Disconnected from AP\n");
            break;
        case BSP_WIFI_EVENT_STA_GOT_IP:
            printf("WiFi Example: Got IP address\n");
            break;
        default:
            break;
    }
}

/**
 * @brief WiFi测试任务
 * @param arg 任务参数
 * @return NULL
 */
static void *wifi_task(const char *arg)
{
    UNUSED(arg);
    int ret;
    char ip_str[32] = {0};

    printf("WiFi Example: Start\n");

    // 注册WiFi事件回调
    bsp_wifi_register_event_handler(wifi_event_handler);

    // 初始化WiFi
    ret = bsp_wifi_init();
    if (ret != 0) {
        printf("WiFi Example: Failed to initialize WiFi\n");
        return NULL;
    }

    // 连接到AP
    ret = bsp_wifi_connect_ap(EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASSWORD);
    if (ret != 0) {
        printf("WiFi Example: Failed to connect to AP\n");
        return NULL;
    }

    // 等待连接
    osal_msleep(5000);

    // 获取IP地址
    ret = bsp_wifi_get_ip(ip_str, sizeof(ip_str));
    if (ret == 0) {
        printf("WiFi Example: IP address = %s\n", ip_str);
    }

    printf("WiFi Example: WiFi connection test completed\n");

    // 持续运行
    while (1) {
        osal_msleep(1000);
    }

    return NULL;
}

/**
 * @brief WiFi示例入口
 * @return 无
 */
static void wifi_example_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("WiFi Example Entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)wifi_task, NULL,
                                      "wifi_task", WIFI_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, WIFI_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("WiFi Example: Failed to set task priority\n");
        }
    } else {
        printf("WiFi Example: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the wifi_example_entry. */
app_run(wifi_example_entry);
