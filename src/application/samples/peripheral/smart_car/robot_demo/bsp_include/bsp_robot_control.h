/**
 ****************************************************************************************************
 * @file        bsp_robot_control.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       智能小车控制BSP层头文件
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：智能小车循迹避障控制
 *
 ****************************************************************************************************
 */

#ifndef __BSP_ROBOT_CONTROL_H__
#define __BSP_ROBOT_CONTROL_H__

#include <stdint.h>
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"

// 小车状态定义
typedef enum {
    CAR_STOP_STATUS = 0,              // 停止状态
    CAR_OBSTACLE_AVOIDANCE_STATUS,    // 避障模式
    CAR_TRACE_STATUS                  // 循迹模式
} CarStatus;

// 小车控制参数
#define CAR_CONTROL_DEMO_TASK_STACK_SIZE   (1024 * 10)
#define CAR_CONTROL_DEMO_TASK_PRIORITY    25
#define DISTANCE_BETWEEN_CAR_AND_OBSTACLE 20  // 障碍物距离阈值 (cm)
#define KEY_INTERRUPT_PROTECT_TIME 30         // 按键中断保护时间 (ms)
#define CAR_TURN_LEFT  0
#define CAR_TURN_RIGHT 1

/**
 * @brief 初始化智能小车控制系统
 * @return 无
 */
void robot_control_init(void);

/**
 * @brief 获取小车当前状态
 * @return 小车状态
 */
CarStatus robot_get_status(void);

/**
 * @brief 设置小车状态
 * @param status 小车状态
 * @return 无
 */
void robot_set_status(CarStatus status);

/**
 * @brief 循迹模式控制
 * @return 无
 */
void robot_trace_mode(void);

/**
 * @brief 避障模式控制
 * @return 无
 */
void robot_obstacle_avoidance_mode(void);

#endif /* __BSP_ROBOT_CONTROL_H__ */
