/**
 ****************************************************************************************************
 * @file        bsp_l9110s.h
 * @author      SkyForever
 * @version     V1.4
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

/**
 * @brief 初始化L9110S电机驱动
 * @return 无
 */
void l9110s_init(void);

/**
 * @brief 设置双轮差速（遥控模式使用）
 * @param left_speed 左轮速度 -100~100
 * @param right_speed 右轮速度 -100~100
 * @return 无
 */
void l9110s_set_differential(int8_t left_speed, int8_t right_speed);

/**
 * @brief 方向命令 → 差速底盘动作（差速底盘可原地转向）
 * @param dir   CarDriveCmd 方向（0停 1前进 2后退 3左转 4右转，见 car_common.h）
 * @param speed 速度幅值 1~100
 * @return 无
 */
void l9110s_drive(uint8_t dir, int8_t speed);

#endif // __BSP_L9110S_H__
