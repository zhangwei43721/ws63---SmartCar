/*
 * Copyright (c) 2024 HiSilicon Technologies CO., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SLE_DEVICE_H
#define SLE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "errcode.h"
#include "sle_errcode.h"

/* ==================== 回调函数类型定义 ==================== */

/**
 * @brief 连接成功回调
 * @param conn_id 连接 ID
 */
typedef void (*sle_connect_cb_t)(uint16_t conn_id);

/**
 * @brief 断开连接回调
 * @param conn_id 连接 ID
 */
typedef void (*sle_disconnect_cb_t)(uint16_t conn_id);

/**
 * @brief 数据接收回调
 * @param data 接收到的数据指针
 * @param len 数据长度
 */
typedef void (*sle_data_recv_cb_t)(const uint8_t* data, uint16_t len);

/* ==================== 对外接口 ==================== */

/**
 * @brief 初始化 SLE 设备（服务器模式）
 * @return ERRCODE_SLE_SUCCESS 成功
 *         ERRCODE_SLE_FAIL 失败
 */
errcode_t sle_device_init(void);

/**
 * @brief 注册连接回调
 * @param cb 回调函数指针
 */
void sle_device_register_connect_callback(sle_connect_cb_t cb);

/**
 * @brief 注册断开连接回调
 * @param cb 回调函数指针
 */
void sle_device_register_disconnect_callback(sle_disconnect_cb_t cb);

/**
 * @brief 注册数据接收回调
 * @param cb 回调函数指针
 */
void sle_device_register_data_callback(sle_data_recv_cb_t cb);

/**
 * @brief 发送数据到已连接的设备
 * @param data 数据指针
 * @param len 数据长度
 * @return ERRCODE_SLE_SUCCESS 成功
 *         ERRCODE_SLE_FAIL 失败（无连接或发送失败）
 */
errcode_t sle_device_send(const uint8_t* data, uint16_t len);

/**
 * @brief 检查是否有设备连接
 * @return true 已连接
 *         false 未连接
 */
bool sle_device_is_connected(void);

#endif /* SLE_DEVICE_H */
