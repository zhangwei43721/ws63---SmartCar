/**
 ****************************************************************************************************
 * @file        bsp_l9110s.c
 * @author      SkyForever
 * @version     V1.6
 * @date        2026-05-29
 * @brief       L9110S电机驱动BSP层实现
 ****************************************************************************************************
 */

#include "bsp_l9110s.h"
#include <stdint.h>
#include "gpio.h"
#include "pinctrl.h"
#include "pwm.h"

#define PWM_PERIOD 500 // 20kHz (50us)

// 驱动电机 PWM 通道
static const uint8_t MOTOR_CH[] = {4, 5, 0, 2};

// 更新指定 PWM 通道的占空比
static void pwm_update(uint8_t ch, uint32_t duty)
{
    pwm_config_t cfg = {.low_time = PWM_PERIOD - duty, .high_time = duty, .repeat = true};
    uapi_pwm_open(ch, &cfg); // 沿用 WS63 沿期重配逻辑
}

// 初始化 L9110S 电机驱动
void l9110s_init(void)
{
    uapi_pwm_init();

    for (int i = 0; i < 4; i++) {
        uapi_pin_set_mode(MOTOR_CH[i], 1);
        uapi_pin_set_ds(MOTOR_CH[i], PIN_DS_7);
        uapi_pin_set_pull(MOTOR_CH[i], PIN_PULL_TYPE_DISABLE);
        pwm_update(MOTOR_CH[i], 0);
    }

    uapi_pwm_set_group(0, (uint8_t *)MOTOR_CH, 4);
    uapi_pwm_start_group(0);
}

// 辅助内联：处理单边电机逻辑
static inline void set_side(uint8_t idx_a, uint8_t idx_b, int8_t speed)
{
    // 死区补偿：L9110S 电机低速无法克服静摩擦转动，将非零的 [1,100]
    // 线性映射到实际可驱动的最小 [50,100]（负向同理）。此物理特性只归本驱动所有。
    if (speed > 0)
        speed = (int8_t)(50 + (speed * 50) / 100);
    else if (speed < 0)
        speed = (int8_t)(-50 + (speed * 50) / 100);

    // 设置占空比
    uint32_t duty = (PWM_PERIOD * (speed < 0 ? -speed : speed)) / 100;

    // 根据方向分配 PWM 通道
    pwm_update(MOTOR_CH[idx_a], (speed < 0) ? duty : 0);
    pwm_update(MOTOR_CH[idx_b], (speed > 0) ? duty : 0);
}

// 设置左右电机差速（-100~100）
void l9110s_set_differential(int8_t left, int8_t right)
{
    set_side(0, 1, left);
    set_side(2, 3, right);
    uapi_pwm_start_group(0); // 重启 group
}