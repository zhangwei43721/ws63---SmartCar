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

#include "sle_test.h"

#include "app_init.h"
#include "common_def.h"
#include "errcode.h"
#include "soc_osal.h"
#include "stdio.h"
#include "string.h"

#include "../../drivers/sle/sle_device.h"

/* ==================== 测试状态 ==================== */

static uint16_t g_test_conn_id = 0;
static uint32_t g_rx_count = 0;
static uint32_t g_tx_count = 0;

/* ==================== 回调函数 ==================== */

static void test_connect_callback(uint16_t conn_id) {
  g_test_conn_id = conn_id;
  printf("[SLE_TEST] 设备已连接, conn_id=%d\r\n", conn_id);
}

static void test_disconnect_callback(uint16_t conn_id) {
  printf("[SLE_TEST] 设备已断开, conn_id=%d\r\n", conn_id);
  printf("[SLE_TEST] RX: %lu, TX: %lu\r\n", g_rx_count, g_tx_count);
  g_test_conn_id = 0;
}

static void test_data_recv_callback(const uint8_t* data, uint16_t len) {
  g_rx_count++;

  // 打印接收到的数据（最多 32 字节）
  printf("[SLE_TEST] 收到数据 [%lu]: ", g_rx_count);
  uint16_t print_len = (len > 32) ? 32 : len;
  for (uint16_t i = 0; i < print_len; i++) {
    printf("%02X ", data[i]);
  }
  if (len > 32) {
    printf("... (共 %d 字节)\r\n", len);
  } else {
    printf("\r\n");
  }

  // 回显测试：将收到的数据原样返回
  errcode_t ret = sle_device_send(data, len);
  if (ret == ERRCODE_SLE_SUCCESS) {
    g_tx_count++;
    printf("[SLE_TEST] 回显数据 [%lu]\r\n", g_tx_count);
  } else {
    printf("[SLE_TEST] 回显失败: %d\r\n", ret);
  }
}

/* ==================== 主任务 ==================== */

static void* sle_test_task(const char* arg) {
  unused(arg);

  printf("[SLE_TEST] SLE 测试应用启动\r\n");
  printf("[SLE_TEST] ========================================\r\n");
  printf("[SLE_TEST] 功能说明:\r\n");
  printf("[SLE_TEST]   1. SLE 设备名: SmartCar_SLE\r\n");
  printf("[SLE_TEST]   2. 收到数据后会原样返回（回显测试）\r\n");
  printf("[SLE_TEST]   3. 连接状态和数据接收会打印到串口\r\n");
  printf("[SLE_TEST] ========================================\r\n");

  // 注册回调
  sle_device_register_connect_callback(test_connect_callback);
  sle_device_register_disconnect_callback(test_disconnect_callback);
  sle_device_register_data_callback(test_data_recv_callback);

  // 初始化 SLE 设备
  errcode_t ret = sle_device_init();
  if (ret != ERRCODE_SLE_SUCCESS) {
    printf("[SLE_TEST] SLE 初始化失败: %d\r\n", ret);
    printf("[SLE_TEST] 测试应用退出\r\n");
    return NULL;
  }

  printf("[SLE_TEST] SLE 初始化成功，等待设备连接...\r\n");

  // 主循环
  while (1) {
    // 每 5 秒打印一次连接状态
    static uint32_t print_count = 0;
    osal_msleep(5000);
    print_count++;

    if (sle_device_is_connected()) {
      printf("[SLE_TEST] 运行中 [%lu] - RX: %lu, TX: %lu\r\n", print_count,
             g_rx_count, g_tx_count);
    } else {
      printf("[SLE_TEST] 等待连接 [%lu]...\r\n", print_count);
    }
  }

  return NULL;
}

/* ==================== 初始化入口 ==================== */

static void sle_test_entry(void) {
  osal_task* task_handle = NULL;

  osal_kthread_lock();
  task_handle = osal_kthread_create((osal_kthread_handler)sle_test_task, NULL,
                                    "sle_test", 0x2000);
  if (task_handle != NULL) {
    osal_kthread_set_priority(task_handle, 24);
    osal_kfree(task_handle);
  }
  osal_kthread_unlock();
}

/* Run the SLE test application. */
app_run(sle_test_entry);
