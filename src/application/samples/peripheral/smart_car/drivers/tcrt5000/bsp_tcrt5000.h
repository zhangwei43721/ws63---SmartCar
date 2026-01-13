/**
 ****************************************************************************************************
 * @file        bsp_tcrt5000.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       TCRT5000红外循迹传感器BSP层头文件
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：TCRT5000红外循迹传感器检测黑线
 *
 ****************************************************************************************************
 */

#ifndef __BSP_TCRT5000_H__
#define __BSP_TCRT5000_H__

#include <stdint.h>
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"

// TCRT5000红外循迹传感器引脚定义
#define TCRT5000_LEFT_GPIO 4   // 左侧传感器（需要PIN_MODE_2）
#define TCRT5000_MIDDLE_GPIO 2 // 中间传感器（需要PIN_MODE_0）
#define TCRT5000_RIGHT_GPIO 0  // 右侧传感器（需要PIN_MODE_0）

// 传感器状态定义
#define TCRT5000_ON_BLACK 0 // 检测到黑线
#define TCRT5000_ON_WHITE 1 // 检测到白色/无黑线

/**
 * @brief 初始化TCRT5000红外循迹传感器
 * @return 无
 */
void tcrt5000_init(void);

/**
 * @brief 获取左侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_left(void);

/**
 * @brief 获取中间传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_middle(void);

/**
 * @brief 获取右侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_right(void);

#endif /* __BSP_TCRT5000_H__ */
