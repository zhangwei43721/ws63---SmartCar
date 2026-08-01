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
 * @file sle_channel.c
 * @brief SLE 控制通道实现 - 复用统一遥控协议
 * @details 协议格式与 UDP 遥控保持一致；本通道只做翻译，决策交 core/car_ctrl
 */

#include "sle_channel.h"

#include "../../../drivers/sle/sle_device.h"
#include "../car_common.h"
#include "../core/car_ctrl.h"
#include "common_def.h"
#include "errcode.h"
#include "securec.h"
#include "soc_osal.h"
#include <stdio.h>

// ==================== 内部状态 ====================

// 连接状态
static bool g_connected = false; // SLE 设备是否已连接

// ==================== 内部辅助函数 ====================

// 中枢应答回调：ACK 从本通道经 SLE 链路发回（"从哪来回哪去"）
static void sle_reply(void *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    (void)sle_device_send(data, len);
}

// 解析SLE接收到的数据包，投递统一命令总线（拷贝后由 car_ctrl 任务串行处理，
// 避免在 SLE 协议栈回调上下文里跑业务逻辑）
static void process_packet(const uint8_t *data, uint16_t len)
{
    if (len < 1)
        return;

    if (len > CAR_CMD_MAX_PAYLOAD) {
        printf("[SLE_SRV] 包过长丢弃: type=0x%02X len=%d\r\n", data[0], len);
        return;
    }

    car_cmd_t cmd = {.source = MODE_SRC_SLE, .reply = sle_reply, .reply_ctx = NULL, .len = len};
    if (memcpy_s(cmd.data, sizeof(cmd.data), data, len) != EOK) {
        return;
    }
    (void)car_ctrl_post_cmd(&cmd);
}

// ==================== SLE 设备回调 ====================

// SLE设备连接回调，标记连接状态
static void sle_connect_callback(uint16_t conn_id)
{
    unused(conn_id);
    g_connected = true;
    printf("[SLE_SRV] 设备已连接\r\n");
}

// SLE设备断开回调，清除连接状态并请求停车（是否放行由中枢安全网关裁决）
static void sle_disconnect_callback(uint16_t conn_id)
{
    unused(conn_id);
    g_connected = false;
    // 断开时请求停车：仅遥控模式下生效，自动模式（循迹/避障）的电机归模式自己管
    car_ctrl_manual_drive(0, 0, MODE_SRC_SLE);
    printf("[SLE_SRV] 设备已断开\r\n");
}

// SLE数据接收回调，转发给process_packet处理
static void sle_data_recv_callback(const uint8_t *data, uint16_t len)
{
    process_packet(data, len);
}

// ==================== 对外接口实现 ====================

// 初始化SLE控制通道：注册回调并启动SLE设备
void sle_channel_init(void)
{
    printf("[SLE_SRV] 初始化 SLE 控制通道...\r\n");

    sle_device_register_connect_callback(sle_connect_callback);
    sle_device_register_disconnect_callback(sle_disconnect_callback);
    sle_device_register_data_callback(sle_data_recv_callback);

    errcode_t ret = sle_device_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE_SRV] SLE 设备初始化失败: %d\r\n", ret);
        return;
    }

    printf("[SLE_SRV] SLE 控制通道初始化完成\r\n");
}
