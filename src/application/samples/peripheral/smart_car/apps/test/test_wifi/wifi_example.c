/**
 ****************************************************************************************************
 * @file        wifi_example.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
 * @brief       WiFi STA 连接示例 —— 直接调用 drivers 层同步接口
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include "../../drivers/wifi_client/bsp_wifi_sta.h"

#define WIFI_TASK_STACK_SIZE 0x2000 // WiFi任务栈大小

#define EXAMPLE_WIFI_SSID "BS-8"           // WiFi热点名称
#define EXAMPLE_WIFI_PASSWORD "BS88888888" // WiFi热点密码

// WiFi 连接测试任务：同步连接 AP 并获取 IP 地址
static void wifi_task_entry(const char *arg)
{
    UNUSED(arg);
    char ip_str[32] = {0};

    printf("[WiFi Example] Start\r\n");

    // 同步阻塞连接（固定等待 20 秒）
    int ret = bsp_wifi_connect_sync(EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASSWORD);
    if (ret != 0) {
        printf("[WiFi Example] Failed to connect to AP\r\n");
        return;
    }

    bsp_wifi_get_ip(ip_str, sizeof(ip_str));
    printf("[WiFi Example] IP address = %s\r\n", ip_str);
    printf("[WiFi Example] WiFi connection test completed\r\n");

    while (1) {
        osDelay(1000);
    }
}

// WiFi 测试示例入口：创建 WiFi 连接任务
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

app_run(wifi_example_entry);
