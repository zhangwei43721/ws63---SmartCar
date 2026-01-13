/**
 ****************************************************************************************************
 * @file        bsp_bt_spp.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       蓝牙SPP BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 * 注意：此模块为框架代码，需要根据WS63的实际蓝牙SPP API进行调整
 *
 ****************************************************************************************************
 */

#include "bsp_bt_spp.h"
#include "soc_osal.h"
#include <string.h>
#include <stdio.h>

static bsp_bt_spp_data_handler_t g_data_handler = NULL;
static bsp_bt_spp_event_handler_t g_event_handler = NULL;
static bsp_bt_spp_status_t g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE;

// 临时数据缓冲区
#define BT_SPP_BUFFER_SIZE 128
static uint8_t g_recv_buffer[BT_SPP_BUFFER_SIZE];

/**
 * @brief 触发SPP事件回调
 * @param event SPP事件
 * @param data 事件数据
 */
static void bsp_bt_spp_trigger_event(bsp_bt_spp_event_t event, void *data)
{
    if (g_event_handler != NULL) {
        g_event_handler(event, data);
    }
}

/**
 * @brief SPP数据接收处理（内部使用）
 * @param data 接收到的数据
 * @param len 数据长度
 */
static void bsp_bt_spp_handle_data(const uint8_t *data, uint32_t len)
{
    if (g_data_handler != NULL) {
        g_data_handler(data, len);
    }
}

/**
 * @brief 初始化蓝牙SPP
 * @param device_name 蓝牙设备名称
 * @return 0成功，-1失败
 */
int bsp_bt_spp_init(const char *device_name)
{
    if (device_name == NULL) {
        printf("BSP BT SPP: Invalid device name\n");
        return -1;
    }

    printf("BSP BT SPP: Initializing with name '%s'...\n", device_name);

    // TODO: 根据WS63的蓝牙API进行初始化
    // 1. 使能蓝牙
    // 2. 设置设备名称
    // 3. 注册SPP服务
    // 4. 开始广播/监听连接

    g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE;
    printf("BSP BT SPP: Initialized successfully\n");

    return 0;
}

/**
 * @brief 等待SPP连接
 * @param timeout_ms 超时时间（毫秒），0表示一直等待
 * @return 0成功，-1失败或超时
 */
int bsp_bt_spp_wait_connection(uint32_t timeout_ms)
{
    printf("BSP BT SPP: Waiting for connection...\n");

    // TODO: 根据WS63的蓝牙API接受SPP连接
    // 这里只是框架代码

    g_bt_spp_status = BSP_BT_SPP_STATUS_CONNECTED;
    bsp_bt_spp_trigger_event(BSP_BT_SPP_EVENT_CONNECTED, NULL);

    printf("BSP BT SPP: Connected\n");
    return 0;
}

/**
 * @brief 发送数据
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 实际发送的长度，-1表示失败
 */
int bsp_bt_spp_send(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }

    if (g_bt_spp_status != BSP_BT_SPP_STATUS_CONNECTED) {
        printf("BSP BT SPP: Not connected\n");
        return -1;
    }

    // TODO: 根据WS63的蓝牙SPP API发送数据
    printf("BSP BT SPP: Sending %u bytes\n", len);

    return (int)len;
}

/**
 * @brief 断开SPP连接
 * @return 0成功，-1失败
 */
int bsp_bt_spp_disconnect(void)
{
    printf("BSP BT SPP: Disconnecting...\n");

    // TODO: 根据WS63的蓝牙API断开连接

    g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE;
    bsp_bt_spp_trigger_event(BSP_BT_SPP_EVENT_DISCONNECTED, NULL);

    return 0;
}

/**
 * @brief 获取SPP连接状态
 * @return SPP状态
 */
bsp_bt_spp_status_t bsp_bt_spp_get_status(void)
{
    return g_bt_spp_status;
}

/**
 * @brief 注册数据接收回调
 * @param handler 数据接收处理函数
 * @return 0成功，-1失败
 */
int bsp_bt_spp_register_data_handler(bsp_bt_spp_data_handler_t handler)
{
    g_data_handler = handler;
    return 0;
}

/**
 * @brief 注册事件回调
 * @param handler 事件处理函数
 * @return 0成功，-1失败
 */
int bsp_bt_spp_register_event_handler(bsp_bt_spp_event_handler_t handler)
{
    g_event_handler = handler;
    return 0;
}
