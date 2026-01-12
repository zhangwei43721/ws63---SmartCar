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
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"

// HC-SR04引脚定义
#define HCSR04_TRIG_GPIO 11 // 触发信号引脚
#define HCSR04_ECHO_GPIO 12 // 回响信号引脚

// GPIO功能号
#define HCSR04_GPIO_FUNC 0

// 测距参数
#define HCSR04_MAX_DISTANCE 400 // 最大测量距离 (cm)
#define HCSR04_MIN_DISTANCE 2   // 最小测量距离 (cm)
#define HCSR04_TIMEOUT_US 30000 // 超时时间 (us)

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
