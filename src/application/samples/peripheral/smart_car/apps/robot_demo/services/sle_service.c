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
#include "../core/motor_executor.h"
#include "../robot_common.h"
#include "../robot_common.h"
#include "common_def.h"
#include "errcode.h"
#include "soc_osal.h"
#include "stdio.h"

// ==================== 协议定义（与 UDP 保持一致） ====================

#pragma pack(1)
typedef struct {
    uint8_t type; // 01=控制, 02=状态, 03=模式, 04=PID, FE=心跳, FF=广播
    uint8_t cmd;
    int8_t motor1;
    int8_t motor2;
    int8_t ir_data;
} sle_packet_t;
#pragma pack()

// ==================== 内部状态 ====================

// 连接状态
static bool g_connected = false;

// ==================== 内部辅助函数 ====================

/**
 * @brief 处理接收到的数据包
 */
static void process_packet(const uint8_t *data, uint16_t len)
{
    if (len < 1)
        return;

    if (len == sizeof(sle_packet_t)) {
        const sle_packet_t *pkt = (const sle_packet_t *)data;

        switch (pkt->type) {
            case 0x01: // 控制包：直接推入 Motor 队列
                motor_executor_push_cmd(pkt->motor1, pkt->motor2);
                break;

            case 0x03: // 模式切换
                if (pkt->cmd <= 4) {
                    printf("[SLE_SRV] 模式切换: %d\r\n", pkt->cmd);
                    robot_mgr_post_mode((CarStatus)pkt->cmd, MODE_SRC_SLE);
                }
                break;

            case 0x04:
                printf("[SLE_SRV] PID 设置 (暂不支持)\r\n");
                break;

            case 0xFE: // 心跳包
                break;

            default:
                printf("[SLE_SRV] 未知包类型: 0x%02X\r\n", pkt->type);
                break;
        }
    } else {
        printf("[SLE_SRV] 包长度错误: %d (期望 %zu)\r\n", len, sizeof(sle_packet_t));
    }
}

// ==================== SLE 设备回调 ====================

static void sle_connect_callback(uint16_t conn_id)
{
    unused(conn_id);
    g_connected = true;
    printf("[SLE_SRV] 设备已连接\r\n");
}

static void sle_disconnect_callback(uint16_t conn_id)
{
    unused(conn_id);
    g_connected = false;
    // 断开时主动停车
    motor_executor_push_cmd(0, 0);
    printf("[SLE_SRV] 设备已断开\r\n");
}

static void sle_data_recv_callback(const uint8_t *data, uint16_t len)
{
    process_packet(data, len);
}

// ==================== 对外接口实现 ====================

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

bool sle_service_is_connected(void)
{
    return g_connected;
}
