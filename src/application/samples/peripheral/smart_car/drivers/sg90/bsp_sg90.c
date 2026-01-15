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
static volatile unsigned int g_target_angle = 90;
// 线程控制标记
static volatile int g_sg90_running = 0;

/**
 * @brief 单次PWM脉冲发送函数 (阻塞20ms)
 *        这个函数现在由后台线程专用
 */
static void sg90_pwm_step(void)
{
    // 1. 计算高电平时间 (us)
    unsigned int high_time_us =
        SG90_PULSE_0_DEG + (unsigned int)((g_target_angle * (SG90_PULSE_180_DEG - SG90_PULSE_0_DEG)) / 180.0);

    // 保护
    if (high_time_us >= SG90_PWM_PERIOD_US)
        high_time_us = SG90_PWM_PERIOD_US - 100;

    unsigned int low_time_us = SG90_PWM_PERIOD_US - high_time_us;

    // 2. 发送波形
    SG90_PIN_SET(1);
    uapi_tcxo_delay_us(high_time_us);

    SG90_PIN_SET(0);
    uapi_tcxo_delay_us(low_time_us);
}

/**
 * @brief SG90 后台守护任务
 *        这就像一个勤劳的工人，一直在后台发波
 */
static void *sg90_daemon_task(const char *arg)
{
    (void)arg;
    printf("SG90: Daemon task started.\n");

    while (g_sg90_running) {
        // 不停地发送PWM波，保持舵机力矩
        sg90_pwm_step();

        osal_msleep(100);
    }
    return NULL;
}

/**
 * @brief 初始化SG90舵机并启动后台线程
 */
void sg90_init(void)
{
    printf("SG90: Init (GPIO Mode with Daemon Task)...\n");

    // 1. GPIO 初始化
    uapi_pin_set_mode(SG90_GPIO, SG90_GPIO_FUNC);
    uapi_gpio_set_dir(SG90_GPIO, GPIO_DIRECTION_OUTPUT);
    SG90_PIN_SET(0);

    // 2. 启动后台发波任务
    if (g_sg90_running == 0) {
        g_sg90_running = 1;
        osal_task *task_handle = osal_kthread_create((osal_kthread_handler)sg90_daemon_task, NULL, "Sg90Daemon", 0x800);
        if (task_handle != NULL) {
            // 舵机任务优先级要高一点，保证波形准，但不能比系统关键任务高
            osal_kthread_set_priority(task_handle, 20);
            printf("SG90: Daemon task created success.\n");
        } else {
            printf("SG90: Error! Failed to create daemon task!\n");
        }
    }
}

/**
 * @brief 设置舵机角度 (非阻塞，瞬间完成)
 *        外部代码调用这个函数后，后台线程会自动读取新角度并执行
 * @param angle 角度值 (0-180)
 */
void sg90_set_angle(unsigned int angle)
{
    if (angle > SG90_ANGLE_MAX)
        angle = SG90_ANGLE_MAX;

    // 原子操作更新变量即可
    g_target_angle = angle;
    // printf("SG90: Angle set to %d\n", angle); // 调试时可打开
}

/**
 * @brief 获取当前目标角度
 */
unsigned int sg90_get_angle(void)
{
    return g_target_angle;
}