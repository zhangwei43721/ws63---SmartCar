/**
 ****************************************************************************************************
 * @file        bsp_bt_spp.h
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
 * @brief       蓝牙SPP BSP层头文件 (基于BLE GATT透传)
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 * 使用说明:
 * 1. 服务UUID: 0xABCD
 * 2. 特征UUID: 0xCDEF (支持Read/Write/Notify)
 * 3. 客户端需要先写入CCCD启用Notify才能接收数据
 *
 ****************************************************************************************************
 */

#ifndef __BSP_BT_SPP_H__
#define __BSP_BT_SPP_H__

#include <stdint.h>

/* 蓝牙SPP连接状态 */
typedef enum {
    BSP_BT_SPP_STATUS_IDLE = 0,     /* 空闲：未连接状态 */
    BSP_BT_SPP_STATUS_CONNECTING,   /* 连接中：正在等待客户端连接 */
    BSP_BT_SPP_STATUS_CONNECTED,    /* 已连接：客户端已连接，可进行数据传输 */
    BSP_BT_SPP_STATUS_DISCONNECTED, /* 已断开：连接已断开 */
} bsp_bt_spp_status_t;

/* 蓝牙SPP事件类型 */
typedef enum {
    BSP_BT_SPP_EVENT_CONNECTED = 0, /* 已连接：客户端成功建立连接 */
    BSP_BT_SPP_EVENT_DISCONNECTED,  /* 已断开：客户端断开连接 */
    BSP_BT_SPP_EVENT_DATA_RECEIVED, /* 接收到数据：收到客户端发送的数据 */
} bsp_bt_spp_event_t;

/* 蓝牙SPP数据接收回调函数类型 */
typedef void (*bsp_bt_spp_data_handler_t)(const uint8_t *data, uint32_t len);

/* 蓝牙SPP事件回调函数类型 */
typedef void (*bsp_bt_spp_event_handler_t)(bsp_bt_spp_event_t event, void *data);

/**
 * @brief 初始化蓝牙SPP
 * @param device_name 蓝牙设备名称
 * @return 0成功，-1失败
 */
int bsp_bt_spp_init(const char *device_name);

/**
 * @brief 发送数据 (通过Notify发送，客户端需先启用Notify)
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 实际发送的长度，-1表示失败，-2表示未启用Notify
 */
int bsp_bt_spp_send(const uint8_t *data, uint32_t len);

/**
 * @brief 获取SPP连接状态
 * @return SPP状态
 */
bsp_bt_spp_status_t bsp_bt_spp_get_status(void);

/**
 * @brief 注册数据接收回调
 * @param handler 数据接收处理函数
 * @return 0成功
 */
int bsp_bt_spp_register_data_handler(bsp_bt_spp_data_handler_t handler);

/**
 * @brief 注册事件回调
 * @param handler 事件处理函数
 * @return 0成功
 */
int bsp_bt_spp_register_event_handler(bsp_bt_spp_event_handler_t handler);

#endif /* __BSP_BT_SPP_H__ */
