/**
 ****************************************************************************************************
 * @file        bsp_l9110s.c
 * @author      SkyForever
 * @version     V1.4
 * @date        2025-01-12
 * @brief       L9110S电机驱动BSP层实现（硬件PWM版本）
 ****************************************************************************************************
 */

#include "bsp_l9110s.h"

#include <stdint.h>

#include "gpio.h"

#define PWM_PERIOD 50 // 20kHz (50us)
#include "pinctrl.h"
#include "pwm.h"
#include "soc_osal.h"

// 驱动电机 PWM 通道：4, 5, 0, 2
static const uint8_t MOTOR_CH[] = {4, 5, 0, 2};

// 保护 PWM 占空更新（差速调用可能来自多个任务：UDP/SLE/语音）
static osal_mutex g_motor_lock;
static bool g_motor_lock_inited = false;

/* 更新指定 PWM 通道的占空比 */
static void pwm_update(uint8_t ch, uint32_t duty)
{
    // WS63(V151) 无 uapi_pwm_update_duty_ratio，运行期沿用 open 重配；用 mutex 保证并发安全
    if (duty > PWM_PERIOD)
        duty = PWM_PERIOD;
    pwm_config_t cfg = {.low_time = PWM_PERIOD - duty, .high_time = duty, .repeat = true};
    uapi_pwm_open(ch, &cfg);
}

/* 初始化 L9110S 电机驱动：初始化 PWM、互斥锁，配置引脚并启动 PWM 组 */
void l9110s_init(void)
{
    uapi_pwm_init();

    if (!g_motor_lock_inited) {
        if (osal_mutex_init(&g_motor_lock) == OSAL_SUCCESS)
            g_motor_lock_inited = true;
    }

    for (int i = 0; i < 4; i++) {
        uapi_pin_set_mode(MOTOR_CH[i], 1);
        pwm_update(MOTOR_CH[i], 0);
    }

    uapi_pwm_set_group(0, (uint8_t *)MOTOR_CH, 4);
    uapi_pwm_start_group(0);
}

// 辅助内联：处理单边电机逻辑
static inline void set_side(uint8_t idx_a, uint8_t idx_b, int8_t speed)
{
    // 1. 限幅
    if (speed > 100)
        speed = 100;
    if (speed < -100)
        speed = -100;

    // 设置占空比
    uint32_t duty = (PWM_PERIOD * (speed < 0 ? -speed : speed)) / 100;

    // 2. 根据方向设置 A/B 通道 (利用三元运算符)
    // Speed > 0: A=0,    B=Duty (前进)
    // Speed < 0: A=Duty, B=0    (后退)
    pwm_update(MOTOR_CH[idx_a], (speed < 0) ? duty : 0);
    pwm_update(MOTOR_CH[idx_b], (speed > 0) ? duty : 0);
}

/* 设置左右电机差速（-100~100），加锁保证并发安全 */
void l9110s_set_differential(int8_t left, int8_t right)
{
    if (g_motor_lock_inited)
        (void)osal_mutex_lock(&g_motor_lock);
    set_side(0, 1, left);
    set_side(2, 3, right);
    // pwm_update 内部 uapi_pwm_open 会让通道脱离 group 启动状态，需要重新 start group
    uapi_pwm_start_group(0);
    if (g_motor_lock_inited)
        (void)osal_mutex_unlock(&g_motor_lock);
}