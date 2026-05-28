/**
 ****************************************************************************************************
 * @file        bsp_hcsr04.c
 * @brief       HC-SR04 超声波测距（中断 + 信号量 / 整数计算）
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 */

#include "bsp_hcsr04.h"

#include <stdio.h>

#include "gpio.h"
#include "hal_gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "tcxo.h"

// HC-SR04引脚定义
#define HCSR04_TRIG_GPIO 6
#define HCSR04_ECHO_GPIO 11
#define HCSR04_GPIO_FUNC HAL_PIO_FUNC_GPIO

// 测距参数
#define HCSR04_TIMEOUT_US 40000
#define HCSR04_MIN_DISTANCE_CM 2.0f
#define HCSR04_MAX_DISTANCE_CM 500.0f

// ECHO 上下沿时间戳（us），由中断写入
static volatile uint32_t s_echo_rise_us = 0;
static volatile uint32_t s_echo_fall_us = 0;
static volatile bool s_echo_rose = false;

// 测量完成信号
static osal_semaphore s_done_sem;
static bool s_done_sem_inited = false;
static osal_mutex s_meas_lock;
static bool s_meas_lock_inited = false;

/* ECHO 引脚双边沿中断：记录上升/下降沿时间戳，下降沿释放信号量 */
static void hcsr04_echo_isr(pin_t pin, uintptr_t param)
{
    UNUSED(param);
    uint32_t now = uapi_tcxo_get_us();
    gpio_level_t lv = uapi_gpio_get_val(pin);
    if (lv == GPIO_LEVEL_HIGH) {
        s_echo_rise_us = now;
        s_echo_rose = true;
    } else {
        s_echo_fall_us = now;
        if (s_done_sem_inited) {
            osal_sem_up(&s_done_sem);
        }
    }
}

/* 初始化 HC-SR04：创建信号量和互斥锁，配置 TRIG 输出和 ECHO 双边沿中断 */
void hcsr04_init(void)
{
    if (!s_done_sem_inited) {
        osal_sem_binary_sem_init(&s_done_sem, 0);
        s_done_sem_inited = true;
    }
    if (!s_meas_lock_inited) {
        osal_mutex_init(&s_meas_lock);
        s_meas_lock_inited = true;
    }

    // TRIG: 输出
    uapi_pin_set_mode(HCSR04_TRIG_GPIO, HCSR04_GPIO_FUNC);
    uapi_gpio_set_dir(HCSR04_TRIG_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(HCSR04_TRIG_GPIO, 0);

    // ECHO: 输入 + 双边沿中断
    uapi_pin_set_mode(HCSR04_ECHO_GPIO, HCSR04_GPIO_FUNC);
    uapi_gpio_set_dir(HCSR04_ECHO_GPIO, GPIO_DIRECTION_INPUT);
    (void)uapi_gpio_unregister_isr_func(HCSR04_ECHO_GPIO);
    (void)uapi_gpio_register_isr_func(HCSR04_ECHO_GPIO, GPIO_INTERRUPT_DEDGE, hcsr04_echo_isr);
}

/* 触发一次超声波测距，返回距离（厘米），失败返回 0.0 */
float hcsr04_get_distance(void)
{
    if (!s_done_sem_inited || !s_meas_lock_inited) {
        return 0.0f;
    }

    (void)osal_mutex_lock(&s_meas_lock);

    s_echo_rose = false;
    s_echo_rise_us = 0;
    s_echo_fall_us = 0;
    while (osal_sem_trydown(&s_done_sem) == OSAL_SUCCESS) {
    }

    // 发送 >=10us 触发脉冲
    uapi_gpio_set_val(HCSR04_TRIG_GPIO, GPIO_LEVEL_HIGH);
    uapi_tcxo_delay_us(20);
    uapi_gpio_set_val(HCSR04_TRIG_GPIO, GPIO_LEVEL_LOW);

    uint32_t timeout_ms = (HCSR04_TIMEOUT_US * 2u) / 1000u + 5u;
    if (osal_sem_down_timeout(&s_done_sem, timeout_ms) != OSAL_SUCCESS || !s_echo_rose) {
        (void)osal_mutex_unlock(&s_meas_lock);
        return 0.0f;
    }

    uint32_t pulse_us = s_echo_fall_us - s_echo_rise_us;
    (void)osal_mutex_unlock(&s_meas_lock);

    // distance(mm) = pulse_us * 0.34 / 2 ≈ pulse_us * 17 / 100；下方在整数域比较范围避免引入浮点
    uint32_t distance_mm = (pulse_us * 17u) / 100u;
    if (distance_mm < (uint32_t)(HCSR04_MIN_DISTANCE_CM * 10) ||
        distance_mm > (uint32_t)(HCSR04_MAX_DISTANCE_CM * 10)) {
        return 0.0f;
    }
    return (float)distance_mm / 10.0f;
}
