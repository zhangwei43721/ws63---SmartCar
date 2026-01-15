/**
 ****************************************************************************************************
 * @file        bsp_sg90.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       SG90舵机BSP层头文件 (智能小车专用)
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：SG90舵机角度控制
 *
 ****************************************************************************************************
 */

#ifndef __BSP_SG90_H__
#define __BSP_SG90_H__
#include <stdint.h>
#include "pinctrl.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "tcxo.h"
#include "soc_osal.h"
#include <stdio.h>

// 舵机引脚定义 (JP4 舵机接口 SG1)
// 注意: GPIO_13 也连接了 LED2 (蓝色)，控制舵机时蓝色LED2会闪烁
#define SG90_GPIO 13
#define SG90_GPIO_FUNC HAL_PIO_FUNC_GPIO

// PWM参数定义
#define SG90_PWM_PERIOD_US 20000 // PWM周期 20ms

// 舵机角度范围
#define SG90_ANGLE_MIN 0
#define SG90_ANGLE_MAX 180

// 高电平时间定义 (单位: 微秒)
#define SG90_PULSE_0_DEG 500    // 0.5ms
#define SG90_PULSE_180_DEG 2500 // 2.5ms

#define SG90_PIN_SET(a) uapi_gpio_set_val(SG90_GPIO, a)

void sg90_init(void);
void sg90_set_angle(unsigned int angle);
unsigned int sg90_get_angle(void);

#endif /* __BSP_SG90_H__ */
