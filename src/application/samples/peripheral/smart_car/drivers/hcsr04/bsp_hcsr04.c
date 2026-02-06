/**
 ****************************************************************************************************
 * @file        bsp_hcsr04.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       HC-SR04超声波测距传感器BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：HC-SR04超声波测距模块进行距离测量
 *
 ****************************************************************************************************
 */

#include "bsp_hcsr04.h"

/**
 * @brief 初始化HC-SR04超声波传感器
 * @return 无
 */
void hcsr04_init(void) {
  // 1. 先配置引脚复用为 GPIO 模式
  uapi_pin_set_mode(HCSR04_TRIG_GPIO, HCSR04_GPIO_FUNC);
  uapi_pin_set_mode(HCSR04_ECHO_GPIO, HCSR04_GPIO_FUNC);

  // 2. 初始化触发引脚为输出
  uapi_gpio_set_dir(HCSR04_TRIG_GPIO, GPIO_DIRECTION_OUTPUT);
  uapi_gpio_set_val(HCSR04_TRIG_GPIO, 0);

  // 3. 初始化回响引脚为输入
  uapi_gpio_set_dir(HCSR04_ECHO_GPIO, GPIO_DIRECTION_INPUT);
}

/**
 * @brief 获取距离测量值
 * @return 距离值 (单位: cm), 测量失败返回0
 */
float hcsr04_get_distance(void) {
  unsigned int start_time = 0;
  unsigned int end_time = 0;
  unsigned int pulse_width = 0;
  gpio_level_t echo_value = 0;
  float distance = 0.0;

  // 发送触发信号: 至少10us的高电平脉冲
  uapi_gpio_set_val(HCSR04_TRIG_GPIO, GPIO_LEVEL_HIGH);
  uapi_tcxo_delay_us(20);
  uapi_gpio_set_val(HCSR04_TRIG_GPIO, GPIO_LEVEL_LOW);

  // 等待回响信号变高
  unsigned int timeout = HCSR04_TIMEOUT_US;
  while (timeout > 0) {
    echo_value = uapi_gpio_get_val(HCSR04_ECHO_GPIO);
    if (echo_value == GPIO_LEVEL_HIGH) {
      break;
    }
    uapi_tcxo_delay_us(1);
    timeout--;
  }

  if (timeout == 0) {
    printf("HC-SR04：等待高电平回声超时\n");
    return 0.0;
  }

  // 记录开始时间
  start_time = uapi_tcxo_get_us();

  // 等待回响信号变低
  timeout = HCSR04_TIMEOUT_US;
  while (timeout > 0) {
    echo_value = uapi_gpio_get_val(HCSR04_ECHO_GPIO);
    if (echo_value == GPIO_LEVEL_LOW) {
      break;
    }
    uapi_tcxo_delay_us(1);
    timeout--;
  }

  if (timeout == 0) {
    printf("HC-SR04：等待低回声超时\n");
    return 0.0;
  }

  // 记录结束时间
  end_time = uapi_tcxo_get_us();

  // 计算脉冲宽度
  pulse_width = end_time - start_time;

  // 计算距离: 距离 = 高电平时间 * 声速 / 2
  // 声速约为 340m/s = 0.034 cm/us
  distance = (float)pulse_width * 0.034f / 2.0f;

  // 限制在有效测量范围内 (2cm ~ 500cm)
  // 超出范围的值通常表示测量异常或超出量程
  if (distance < HCSR04_MIN_DISTANCE_CM || distance > HCSR04_MAX_DISTANCE_CM) {
    return 0.0f;
  }

  return distance;
}
