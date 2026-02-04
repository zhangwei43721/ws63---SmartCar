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

#include "common_def.h"
#include "errcode.h"
#include "soc_osal.h"
#include "stdio.h"

#include "../../../drivers/sle/sle_device.h"
#include "../core/robot_mgr.h"
#include "../robot_common.h"

/* ==================== 协议定义（与 UDP 保持一致） ==================== */

#pragma pack(1)
typedef struct {
  uint8_t type;  // 01=控制, 02=状态, 03=模式, 04=PID, FE=心跳, FF=广播
  uint8_t cmd;
  int8_t motor1;
  int8_t motor2;
  int8_t ir_data;
} sle_packet_t;
#pragma pack()

/* ==================== 内部状态 ==================== */

// 命令缓存（与 udp_service 相同的结构）
static struct {
  int8_t m1, m2;
  bool new_data;
} g_cmd_cache = {0};

// 连接状态
static bool g_connected = false;

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 处理接收到的数据包
 * @param data 数据指针
 * @param len 数据长度
 */
static void process_packet(const uint8_t* data, uint16_t len) {
  if (len < 1) return;

  unused(data[0]);  // type 字段，在 switch 中使用

  // 标准5字节包
  if (len == sizeof(sle_packet_t)) {
    const sle_packet_t* pkt = (const sle_packet_t*)data;

    switch (pkt->type) {
      case 0x01:  // 控制包
        // 保存到命令缓存
        g_cmd_cache.m1 = pkt->motor1;
        g_cmd_cache.m2 = pkt->motor2;
        g_cmd_cache.new_data = true;
        printf("[SLE_SRV] 控制命令: m1=%d, m2=%d\r\n", pkt->motor1,
               pkt->motor2);
        break;

      case 0x03:  // 模式切换
        if (pkt->cmd <= 4) {
          printf("[SLE_SRV] 模式切换: %d\r\n", pkt->cmd);
          robot_mgr_set_status((CarStatus)pkt->cmd);
        }
        break;

      case 0x04:  // PID 设置
        // PID 设置命令（暂不支持）
        printf("[SLE_SRV] PID 设置 (暂不支持)\r\n");
        break;

      case 0xFE:  // 心跳包
        // 仅用于保活，无需处理
        break;

      default:
        printf("[SLE_SRV] 未知包类型: 0x%02X\r\n", pkt->type);
        break;
    }
  } else {
    printf("[SLE_SRV] 包长度错误: %d (期望 %zu)\r\n", len,
           sizeof(sle_packet_t));
  }
}

/* ==================== SLE 设备回调 ==================== */

/**
 * @brief 连接成功回调
 */
static void sle_connect_callback(uint16_t conn_id) {
  unused(conn_id);
  g_connected = true;
  printf("[SLE_SRV] 设备已连接\r\n");
}

/**
 * @brief 断开连接回调
 */
static void sle_disconnect_callback(uint16_t conn_id) {
  unused(conn_id);
  g_connected = false;

  // 清空命令缓存
  g_cmd_cache.m1 = 0;
  g_cmd_cache.m2 = 0;
  g_cmd_cache.new_data = false;

  printf("[SLE_SRV] 设备已断开\r\n");
}

/**
 * @brief 数据接收回调
 */
static void sle_data_recv_callback(const uint8_t* data, uint16_t len) {
  // 打印接收到的数据（调试用）
  printf("[SLE_SRV] 收到数据: ");
  uint16_t print_len = (len > 16) ? 16 : len;
  for (uint16_t i = 0; i < print_len; i++) {
    printf("%02X ", data[i]);
  }
  if (len > 16) {
    printf("... (共 %d 字节)\r\n", len);
  } else {
    printf("\r\n");
  }

  // 处理数据包
  process_packet(data, len);
}

/* ==================== 对外接口实现 ==================== */

void sle_service_init(void) {
  printf("[SLE_SRV] 初始化 SLE 遥控服务...\r\n");

  // 注册回调
  sle_device_register_connect_callback(sle_connect_callback);
  sle_device_register_disconnect_callback(sle_disconnect_callback);
  sle_device_register_data_callback(sle_data_recv_callback);

  // 初始化 SLE 设备
  errcode_t ret = sle_device_init();
  if (ret != ERRCODE_SLE_SUCCESS) {
    printf("[SLE_SRV] SLE 设备初始化失败: %d\r\n", ret);
    return;
  }

  printf("[SLE_SRV] SLE 遥控服务初始化完成\r\n");
}

void sle_service_tick(void) {
  // 当前无需周期性任务
  // 未来可用于发送心跳或状态数据
}

bool sle_service_is_connected(void) { return g_connected; }

bool sle_service_pop_cmd(int8_t* motor1_out, int8_t* motor2_out) {
  if (!g_connected || !g_cmd_cache.new_data) {
    return false;
  }

  *motor1_out = g_cmd_cache.m1;
  *motor2_out = g_cmd_cache.m2;
  g_cmd_cache.new_data = false;

  return true;
}
