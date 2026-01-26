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
#define L9110S_LEFT_A_GPIO 7 // MOTOR_IN1 (GPIO_07)
#define L9110S_LEFT_B_GPIO 8 // MOTOR_IN2 (GPIO_08)

// 右轮电机控制引脚
#define L9110S_RIGHT_A_GPIO 5 // MOTOR_IN3 (GPIO_05)
#define L9110S_RIGHT_B_GPIO 6 // MOTOR_IN4 (GPIO_06)

/**
 * @brief 初始化L9110S电机驱动
 * @return 无
 */
void l9110s_init(void);

// 电机控制宏（小朋友更容易理解）
#define CAR_FORWARD()  l9110s_set_differential(100, 100)
#define CAR_BACKWARD() l9110s_set_differential(-100, -100)
#define CAR_LEFT()     l9110s_set_differential(0, 100)
#define CAR_RIGHT()    l9110s_set_differential(100, 0)
#define CAR_STOP()     l9110s_set_differential(0, 0)

/**
 * @brief 设置左轮电机速度和方向
 * @param speed 速度值 -100~100 (负=反转, 0=停止, 正=正转)
 * @return 无
 */
void l9110s_set_left_motor(int8_t speed);

/**
 * @brief 设置右轮电机速度和方向
 * @param speed 速度值 -100~100 (负=反转, 0=停止, 正=正转)
 * @return 无
 */
void l9110s_set_right_motor(int8_t speed);

/**
 * @brief 设置双轮差速（遥控模式使用）
 * @param left_speed 左轮速度 -100~100
 * @param right_speed 右轮速度 -100~100
 * @return 无
 */
void l9110s_set_differential(int8_t left_speed, int8_t right_speed);

#endif /* __BSP_L9110S_H__ */
