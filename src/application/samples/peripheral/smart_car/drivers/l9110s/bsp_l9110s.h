/**
 ****************************************************************************************************
 * @file        bsp_l9110s.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       L9110S电机驱动BSP层头文件
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：L9110S电机控制，实现小车前进、后退、左转、右转、停止
 *
 ****************************************************************************************************
 */

#ifndef __BSP_L9110S_H__
#define __BSP_L9110S_H__

#include <stdint.h>
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"

// L9110S电机驱动引脚定义 (根据硬件原理图)
// 左轮电机控制引脚
#define L9110S_LEFT_A_GPIO 6 // MOTOR_IN1 (GPIO_06)
#define L9110S_LEFT_B_GPIO 7 // MOTOR_IN2 (GPIO_07)

// 右轮电机控制引脚
#define L9110S_RIGHT_A_GPIO 8 // MOTOR_IN3 (GPIO_08)
#define L9110S_RIGHT_B_GPIO 9 // MOTOR_IN4 (GPIO_09)

// GPIO功能号
#define L9110S_GPIO_FUNC 0

/**
 * @brief 初始化L9110S电机驱动
 * @return 无
 */
void l9110s_init(void);

/**
 * @brief 小车前进
 * @return 无
 */
void car_forward(void);

/**
 * @brief 小车后退
 * @return 无
 */
void car_backward(void);

/**
 * @brief 小车左转
 * @return 无
 */
void car_left(void);

/**
 * @brief 小车右转
 * @return 无
 */
void car_right(void);

/**
 * @brief 小车停止
 * @return 无
 */
void car_stop(void);

#endif /* __BSP_L9110S_H__ */
