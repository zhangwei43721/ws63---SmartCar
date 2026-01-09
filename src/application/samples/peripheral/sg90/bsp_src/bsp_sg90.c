/**
 ****************************************************************************************************
 * @file        bsp_sg90.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-09
 * @brief       SG90舵机BSP层实现
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
 * @brief 初始化SG90舵机 (仿照 beep_init)
 */
void sg90_init(void)
{
    printf("SG90: Init (GPIO Mode)...\n");

    // 1. 设置管脚复用为 GPIO
    uapi_pin_set_mode(SG90_GPIO, HAL_PIO_FUNC_GPIO);

    // 2. 设置为输出模式
    uapi_gpio_set_dir(SG90_GPIO, GPIO_DIRECTION_OUTPUT);

    // 3. 默认拉低
    SG90_PIN_SET(0);
}

/**
 * @brief 设置舵机角度 (仿照 beep_alarm 的 while 循环)
 * @note 由于是软件模拟，该函数会阻塞运行约 400ms 以产生波形
 */
void sg90_set_angle(unsigned int angle)
{
    // 限制角度范围
    if (angle > SG90_ANGLE_MAX) {
        angle = SG90_ANGLE_MAX;
    }

    g_current_angle = angle;

    // 1. 计算高电平时间 (us)
    unsigned int high_time_us =
        SG90_PULSE_0_DEG + (unsigned int)((angle * (SG90_PULSE_180_DEG - SG90_PULSE_0_DEG)) / 180.0);

    // 2. 计算低电平时间 (us)
    unsigned int low_time_us = SG90_PWM_PERIOD_US - high_time_us;

    printf("SG90: Angle=%d, High=%d us\n", angle, high_time_us);

    // 3. 发送 PWM 波形
    // SG90 周期为 20ms。发送 20 个周期大约耗时 400ms，足够舵机转动到位。
    // 如果舵机负载大转动慢，可以增加循环次数 (例如 50 次 = 1秒)
    uint16_t cycles = 25;

    while (cycles--) {
        SG90_PIN_SET(1);                  // 拉高
        uapi_tcxo_delay_us(high_time_us); // 延时高电平时间

        SG90_PIN_SET(0);                 // 拉低
        uapi_tcxo_delay_us(low_time_us); // 延时剩余周期时间
    }
}

unsigned int sg90_get_angle(void)
{
    return g_current_angle;
}