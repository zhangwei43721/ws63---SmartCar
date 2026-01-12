/**
 ****************************************************************************************************
 * @file        bsp_sg90.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       SG90舵机BSP层实现 (智能小车专用)
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

#include "../bsp_include/bsp_sg90.h"
#include <stdio.h>

// 当前角度
static unsigned int g_current_angle = 90;

/**
 * @brief 初始化SG90舵机
 */
void sg90_init(void)
{
    printf("SG90: Init (GPIO Mode)...\n");

    // 设置为输出模式
    uapi_gpio_set_dir(SG90_GPIO, GPIO_DIRECTION_OUTPUT);

    // 默认拉低
    uapi_gpio_set_val(SG90_GPIO, GPIO_LEVEL_LOW);
}

/**
 * @brief 设置舵机角度
 * @param angle 角度值 (0-180)
 */
void sg90_set_angle(unsigned int angle)
{
    // 限制角度范围
    if (angle > SG90_ANGLE_MAX) {
        angle = SG90_ANGLE_MAX;
    }

    g_current_angle = angle;

    // 计算高电平时间 (us)
    unsigned int high_time_us =
        SG90_PULSE_0_DEG + (unsigned int)((angle * (SG90_PULSE_180_DEG - SG90_PULSE_0_DEG)) / 180.0);

    // 计算低电平时间 (us)
    unsigned int low_time_us = SG90_PWM_PERIOD_US - high_time_us;

    // 发送 PWM 波形
    // SG90 周期为 20ms，发送 25 个周期
    uint16_t cycles = 25;

    while (cycles--) {
        uapi_gpio_set_val(SG90_GPIO, GPIO_LEVEL_HIGH);    // 拉高
        uapi_tcxo_delay_us(high_time_us);                    // 延时高电平时间

        uapi_gpio_set_val(SG90_GPIO, GPIO_LEVEL_LOW);     // 拉低
        uapi_tcxo_delay_us(low_time_us);                    // 延时剩余周期时间
    }
}

/**
 * @brief 获取当前角度
 * @return 当前角度值
 */
unsigned int sg90_get_angle(void)
{
    return g_current_angle;
}
