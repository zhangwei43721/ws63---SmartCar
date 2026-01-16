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

// 舵机参数
#define SG90_PERIOD_US 20000 // 20ms
#define PULSE_0_DEG 500      // 0.5ms
#define PULSE_180_DEG 2500   // 2.5ms

// 自动休眠配置
// 每次设置角度后，发送多少次脉冲就停止？
// 3次 * 20ms = 60ms。给舵机0.1秒的时间转动到位，足够了。
#define ACTIVE_CYCLES_COUNT 3

// 全局变量
static volatile unsigned int g_target_angle = 90;
static volatile int g_remain_pulses = 0; // 剩余需要发送的脉冲数
static volatile int g_sg90_running = 0;

/**
 * @brief 发送单个PWM脉冲
 */
static void sg90_send_pulse_safe(unsigned int high_time_us)
{
    unsigned int irq_sts;

    // 只在发波的这 0.5~2.5ms 期间关中断
    irq_sts = osal_irq_lock();

    uapi_gpio_set_val(SG90_GPIO, GPIO_LEVEL_HIGH);
    uapi_tcxo_delay_us(high_time_us);
    uapi_gpio_set_val(SG90_GPIO, GPIO_LEVEL_LOW);

    osal_irq_restore(irq_sts);
}

/**
 * @brief SG90 后台守护任务
 */
static void *sg90_daemon_task(const char *arg)
{
    (void)arg;
    unsigned int high_time;
    unsigned int low_time_sim;

    while (g_sg90_running) {

        // 核心逻辑：只有当还有剩余脉冲数时，才发送波形
        if (g_remain_pulses > 0) {

            // 1. 计算脉宽
            high_time = PULSE_0_DEG + (unsigned int)((g_target_angle * (PULSE_180_DEG - PULSE_0_DEG)) / 180.0);
            sg90_send_pulse_safe(high_time);           // 2. 发送波形 (占用中断)
            g_remain_pulses--;                         // 3. 递减计数器
            low_time_sim = SG90_PERIOD_US - high_time; // 4. 计算休眠时间 (凑齐20ms)
            osal_msleep(low_time_sim / 1000);

        } else {
            // 闲置状态：完全释放 CPU，不关中断，不影响网络和超声波
            // 每 100ms 检查一次有没有新指令
            osal_msleep(100);
        }
    }
    return NULL;
}

/**
 * @brief 初始化SG90
 */
void sg90_init(void)
{
    // GPIO 初始化
    uapi_pin_set_mode(SG90_GPIO, 0);
    uapi_gpio_init();
    uapi_gpio_set_dir(SG90_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(SG90_GPIO, GPIO_LEVEL_LOW);

    // 启动后台任务
    if (g_sg90_running == 0) {
        g_sg90_running = 1;
        g_remain_pulses = 0; // 初始时不发波
        osal_task *task = osal_kthread_create((osal_kthread_handler)sg90_daemon_task, NULL, "Sg90Task", 0x800);
        if (task != NULL)
            osal_kthread_set_priority(task, 10); // 优先级较高，但运行时间短
    }
}

/**
 * @brief 设置角度
 * @note  调用此函数会“唤醒”后台任务工作 0.5秒
 */
void sg90_set_angle(unsigned int angle)
{
    if (angle > SG90_ANGLE_MAX)
        angle = SG90_ANGLE_MAX;
    g_target_angle = angle;                // 更新角度
    g_remain_pulses = ACTIVE_CYCLES_COUNT; // 重置计数器
}

unsigned int sg90_get_angle(void)
{
    return g_target_angle;
}