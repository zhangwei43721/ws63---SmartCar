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

/**
 * @file sle_service.c
 * @brief SLE 遥控服务实现 - 复用 UDP 遥控协议
 * @details 协议格式与 UDP 遥控保持一致，确保兼容性
 */

#include "sle_service.h"

#include "../../../drivers/sle/sle_device.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "common_def.h"
#include "errcode.h"
#include "soc_osal.h"
#include <stdio.h>

// ==================== 内部状态 ====================

// 连接状态
static bool g_connected = false; // SLE 设备是否已连接

// ==================== 内部辅助函数 ====================

/**
 * @brief 处理接收到的数据包
 */
/* 解析SLE接收到的数据包，交由统一协议处理器处理 */
static void process_packet(const uint8_t *data, uint16_t len)
{
    if (len < 1)
        return;

    if (len == sizeof(car_packet_t)) {
        const car_packet_t *pkt = (const car_packet_t *)data;

        if (car_proto_handle_packet(pkt, MODE_SRC_SLE))
            return; // CONTROL / MODE / HEARTBEAT 已由统一处理器消费

        // PID 仅 UDP 实现，SLE 收到忽略
        if (pkt->type == CAR_PKT_PID)
            return;

        printf("[SLE_SRV] 未知包类型: 0x%02X\r\n", pkt->type);
    } else {
        printf("[SLE_SRV] 包长度错误: %d (期望 %zu)\r\n", len, sizeof(car_packet_t));
    }
}

// ==================== SLE 设备回调 ====================

/* SLE设备连接回调，标记连接状态 */
static void sle_connect_callback(uint16_t conn_id)
{
    unused(conn_id);
    g_connected = true;
    printf("[SLE_SRV] 设备已连接\r\n");
}

/* SLE设备断开回调，清除连接状态并停车 */
static void sle_disconnect_callback(uint16_t conn_id)
{
    unused(conn_id);
    g_connected = false;
    // 断开时主动停车
    bsp_motor_push_cmd(0, 0);
    printf("[SLE_SRV] 设备已断开\r\n");
}

/* SLE数据接收回调，转发给process_packet处理 */
static void sle_data_recv_callback(const uint8_t *data, uint16_t len)
{
    process_packet(data, len);
}

// ==================== 对外接口实现 ====================

/* 初始化SLE遥控服务：注册回调并启动SLE设备 */
void sle_service_init(void)
{
    printf("[SLE_SRV] 初始化 SLE 遥控服务...\r\n");

    sle_device_register_connect_callback(sle_connect_callback);
    sle_device_register_disconnect_callback(sle_disconnect_callback);
    sle_device_register_data_callback(sle_data_recv_callback);

    errcode_t ret = sle_device_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE_SRV] SLE 设备初始化失败: %d\r\n", ret);
        return;
    }

    printf("[SLE_SRV] SLE 遥控服务初始化完成\r\n");
}
