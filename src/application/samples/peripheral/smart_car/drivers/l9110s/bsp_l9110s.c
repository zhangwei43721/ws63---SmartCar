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
#include "pinctrl.h"
#include "pwm.h"
#include "soc_osal.h"

static void pwm_update(uint8_t ch, uint32_t duty) {
  // 配置结构体 (利用 C99 指定初始化，未指定成员自动为0)
  pwm_config_t cfg = {
      .low_time = PWM_PERIOD - duty, .high_time = duty, .repeat = true};
  uapi_pwm_open(ch, &cfg);  // 打开单通道 PWM
}

void l9110s_init(void) {
  uapi_pwm_init();  // 初始化 PWM

  for (int i = 0; i < 4; i++) {
    uapi_pin_set_mode(MOTOR_CH[i], 1);  // 设置为模式 1 （PWM）
    pwm_update(MOTOR_CH[i], 0);         // 初始占空比 0
  }

  uapi_pwm_set_group(0, (uint8_t*)MOTOR_CH, 4);  // 设置 PWM 分组
  uapi_pwm_start_group(0);                       // 启动分组
}

// 辅助内联：处理单边电机逻辑
static inline void set_side(uint8_t idx_a, uint8_t idx_b, int8_t speed) {
  // 1. 限幅
  if (speed > 100) speed = 100;
  if (speed < -100) speed = -100;

  // 设置占空比
  uint32_t duty = (PWM_PERIOD * (speed < 0 ? -speed : speed)) / 100;

  // 2. 根据方向设置 A/B 通道 (利用三元运算符)
  // Speed > 0: A=0,    B=Duty (前进)
  // Speed < 0: A=Duty, B=0    (后退)
  pwm_update(MOTOR_CH[idx_a], (speed < 0) ? duty : 0);
  pwm_update(MOTOR_CH[idx_b], (speed > 0) ? duty : 0);
}

void l9110s_set_differential(int8_t left, int8_t right) {
  set_side(0, 1, left);     // 设置左轮
  set_side(2, 3, right);    // 设置右轮
  uapi_pwm_start_group(0);  // 使能
}