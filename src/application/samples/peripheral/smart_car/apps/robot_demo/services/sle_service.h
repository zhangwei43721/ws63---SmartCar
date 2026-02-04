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

#ifndef SLE_SERVICE_H
#define SLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化 SLE 遥控服务
 */
void sle_service_init(void);

/**
 * @brief SLE 服务周期任务（可选，用于发送心跳或状态）
 */
void sle_service_tick(void);

/**
 * @brief 检查是否有 SLE 设备连接
 * @return true 已连接
 *         false 未连接
 */
bool sle_service_is_connected(void);

/**
 * @brief 从缓冲区取出一条遥控命令（非阻塞）
 * @param motor1_out 输出：电机1 值 (-100 ~ 100)
 * @param motor2_out 输出：电机2 值 (-100 ~ 100)
 * @return true 有新命令
 *         false 无新命令
 */
bool sle_service_pop_cmd(int8_t* motor1_out, int8_t* motor2_out);

#endif /* SLE_SERVICE_H */
