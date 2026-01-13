/**
 ****************************************************************************************************
 * @file        bsp_bt_spp.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       蓝牙SPP BSP层头文件
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 */

#ifndef __BSP_BT_SPP_H__
#define __BSP_BT_SPP_H__

#include <stdint.h>

// 蓝牙SPP连接状态
typedef enum {
    BSP_BT_SPP_STATUS_IDLE = 0,      // 空闲
    BSP_BT_SPP_STATUS_CONNECTING,    // 连接中
    BSP_BT_SPP_STATUS_CONNECTED,     // 已连接
    BSP_BT_SPP_STATUS_DISCONNECTED,  // 已断开
} bsp_bt_spp_status_t;

// 蓝牙SPP事件类型
typedef enum {
    BSP_BT_SPP_EVENT_CONNECTED = 0,     // 已连接
    BSP_BT_SPP_EVENT_DISCONNECTED,      // 已断开
    BSP_BT_SPP_EVENT_DATA_RECEIVED,     // 接收到数据
} bsp_bt_spp_event_t;

// 蓝牙SPP数据接收回调函数类型
typedef void (*bsp_bt_spp_data_handler_t)(const uint8_t *data, uint32_t len);

// 蓝牙SPP事件回调函数类型
typedef void (*bsp_bt_spp_event_handler_t)(bsp_bt_spp_event_t event, void *data);

/**
 * @brief 初始化蓝牙SPP
 * @param device_name 蓝牙设备名称
 * @return 0成功，-1失败
 */
int bsp_bt_spp_init(const char *device_name);

/**
 * @brief 等待SPP连接
 * @param timeout_ms 超时时间（毫秒），0表示一直等待
 * @return 0成功，-1失败或超时
 */
int bsp_bt_spp_wait_connection(uint32_t timeout_ms);

/**
 * @brief 发送数据
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 实际发送的长度，-1表示失败
 */
int bsp_bt_spp_send(const uint8_t *data, uint32_t len);

/**
 * @brief 断开SPP连接
 * @return 0成功，-1失败
 */
int bsp_bt_spp_disconnect(void);

/**
 * @brief 获取SPP连接状态
 * @return SPP状态
 */
bsp_bt_spp_status_t bsp_bt_spp_get_status(void);

/**
 * @brief 注册数据接收回调
 * @param handler 数据接收处理函数
 * @return 0成功，-1失败
 */
int bsp_bt_spp_register_data_handler(bsp_bt_spp_data_handler_t handler);

/**
 * @brief 注册事件回调
 * @param handler 事件处理函数
 * @return 0成功，-1失败
 */
int bsp_bt_spp_register_event_handler(bsp_bt_spp_event_handler_t handler);

#endif /* __BSP_BT_SPP_H__ */
