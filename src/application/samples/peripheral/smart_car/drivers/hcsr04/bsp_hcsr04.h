/**
 ****************************************************************************************************
 * @file        bsp_hcsr04.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       HC-SR04超声波测距传感器BSP层头文件
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：HC-SR04超声波测距模块进行距离测量
 *
 ****************************************************************************************************
 */

#ifndef __BSP_HCSR04_H__
#define __BSP_HCSR04_H__

#include <stdint.h>
#include <stdio.h>

#include "gpio.h"
#include "hal_gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "tcxo.h"

// HC-SR04引脚定义
#define HCSR04_TRIG_GPIO 6   // 触发信号引脚
#define HCSR04_ECHO_GPIO 11  // 回响信号引脚

// GPIO功能号
#define HCSR04_GPIO_FUNC HAL_PIO_FUNC_GPIO

// 测距参数
#define HCSR04_TIMEOUT_US 40000  // 超时时间 (us)

// HC-SR04 有效测量范围: 2cm ~ 500cm
#define HCSR04_MIN_DISTANCE_CM 2.0f
#define HCSR04_MAX_DISTANCE_CM 500.0f

/**
 * @brief 初始化HC-SR04超声波传感器
 * @return 无
 */
void hcsr04_init(void);

/**
 * @brief 获取距离测量值
 * @return 距离值 (单位: cm), 测量失败返回0
 */
float hcsr04_get_distance(void);

#endif /* __BSP_HCSR04_H__ */
