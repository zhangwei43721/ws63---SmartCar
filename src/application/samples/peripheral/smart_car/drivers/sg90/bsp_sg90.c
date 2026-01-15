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

#include "bsp_sg90.h"

// 全局变量：存储目标角度
static unsigned int g_target_angle = 90;
/**
 * @brief 初始化SG90舵机
 */
void sg90_init(void)
{
    printf("SG90: Init (GPIO Mode)...\n");
    uapi_pin_set_mode(SG90_GPIO, SG90_GPIO_FUNC);        // 复用为 GPIO
    uapi_gpio_set_dir(SG90_GPIO, GPIO_DIRECTION_OUTPUT); // 输出模式
    SG90_PIN_SET(0);                                     // 默认拉低
}

/**
 * @brief 设置舵机角度
 * @param angle 角度值 (0-180)
 */
void sg90_set_angle(unsigned int angle)
{
    if (angle > SG90_ANGLE_MAX)
        angle = SG90_ANGLE_MAX;
    g_target_angle = angle;
}

/**
 * @brief 获取当前目标角度
 */
unsigned int sg90_get_angle(void)
{
    return g_target_angle;
}

/**
 * @brief 执行一次PWM脉冲 (阻塞20ms)
 * @note  需要在主任务的 while(1) 中不断调用此函数，舵机才有力气
 */
void sg90_pwm_step(void)
{
    // 1. 计算高电平时间 (us)，根据 g_target_angle 实时计算
    unsigned int high_time_us =
        SG90_PULSE_0_DEG + (unsigned int)((g_target_angle * (SG90_PULSE_180_DEG - SG90_PULSE_0_DEG)) / 180.0);

    // 保护：防止计算溢出导致 high_time_us 大于周期
    if (high_time_us >= SG90_PWM_PERIOD_US)
        high_time_us = SG90_PWM_PERIOD_US - 100;

    // 2. 计算低电平时间 (us)
    unsigned int low_time_us = SG90_PWM_PERIOD_US - high_time_us;

    // 3. 发送 单个 PWM 周期 (耗时约 20ms)
    SG90_PIN_SET(1);
    uapi_tcxo_delay_us(high_time_us);

    SG90_PIN_SET(0);
    uapi_tcxo_delay_us(low_time_us);
}