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

// 驱动电机GPIO口:  4, 5, 0, 2
static const uint8_t MOTOR_CH[] = {4, 5, 0, 2};
#define PWM_PERIOD 50 // 20kHz (50us)

/**
 * @brief 初始化L9110S电机驱动
 * @return 无
 */
void l9110s_init(void);

// 电机控制宏
#define CAR_FORWARD() l9110s_set_differential(100, 100)
#define CAR_BACKWARD() l9110s_set_differential(-100, -100)
#define CAR_LEFT() l9110s_set_differential(0, 100)
#define CAR_RIGHT() l9110s_set_differential(100, 0)
#define CAR_STOP() l9110s_set_differential(0, 0)

/**
 * @brief 设置双轮差速（遥控模式使用）
 * @param left_speed 左轮速度 -100~100
 * @param right_speed 右轮速度 -100~100
 * @return 无
 */
void l9110s_set_differential(int8_t left_speed, int8_t right_speed);

#endif /* __BSP_L9110S_H__ */
