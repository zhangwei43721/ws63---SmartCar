/**
 ****************************************************************************************************
 * @file        robot_demo.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-16
 * @brief       LiteOS 智能小车循迹避障示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 */

#include "app_init.h"
#include "common_def.h"
#include "core/robot_config.h"
#include "core/robot_mgr.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "osal_timer.h"
#include "pinctrl.h"
#include "services/voice_service.h"
#include "soc_osal.h"
#include "watchdog.h"

#define ROBOT_MODE_SWITCH_GPIO 3  // 按键 设置为GPIO 3

// 上次按键触发的时间戳（用于按键防抖，避免抖动时多次触发）
static unsigned long long button_time_tick = 0;

/**
 * @brief 按键中断：停止 -> 循迹 -> 避障 -> WiFi -> 停止
 */
static void mode_switch_isr(pin_t pin, uintptr_t param) {
  UNUSED(pin);
  UNUSED(param);

  unsigned long long current_tick = osal_get_jiffies();
  if ((current_tick - button_time_tick) < osal_msecs_to_jiffies(200)) {
    return;  // 防抖
  }
  button_time_tick = current_tick;

  CarStatus current_status = robot_mgr_get_status();

  // 模式循环切换：停止 -> 循迹 -> 避障 -> 遥控 -> 停止
  CarStatus next_status = (CarStatus)((current_status + 1) % 4);

  // 打印切换信息
  const char* mode_names[] = {"停止", "循迹", "避障", "遥控"};
  printf("模式切换：%s -> %s\r\n", mode_names[current_status],
         mode_names[next_status]);

  robot_mgr_set_status(next_status);
}

/**
 * @brief 初始化按键及中断
 */
static void robot_key_init(void) {
  // 配置 GPIO 功能、方向及上拉
  uapi_pin_set_mode(ROBOT_MODE_SWITCH_GPIO, HAL_PIO_FUNC_GPIO);
  uapi_gpio_set_dir(ROBOT_MODE_SWITCH_GPIO, GPIO_DIRECTION_INPUT);
  uapi_pin_set_pull(ROBOT_MODE_SWITCH_GPIO, PIN_PULL_TYPE_UP);

  // 注册下降沿中断
  uapi_gpio_register_isr_func(ROBOT_MODE_SWITCH_GPIO,
                              GPIO_INTERRUPT_FALLING_EDGE, mode_switch_isr);
}

/**
 * @brief 智能小车主任务
 */
static void* robot_demo_task(const char* arg) {
  UNUSED(arg);

  robot_mgr_init();      // 初始化底层驱动
  voice_service_init();  // 初始化UART命令服务
  robot_key_init();      // 初始化按键控制

  while (1) {
    robot_mgr_tick();      // 执行小车逻辑
    voice_service_tick();  // 执行UART命令服务
    uapi_watchdog_kick();  // 喂狗
    osal_msleep(20);       // 调度让权延时
  }

  return NULL;
}

/**
 * @brief 任务入口
 */
static void robot_demo_entry(void) {
  osal_task* task_handle = NULL;
  // 创建 OSAL 线程
  osal_kthread_lock();
  task_handle = osal_kthread_create((osal_kthread_handler)robot_demo_task, NULL,
                                    "robot_demo_task", TASK_STACK_SIZE);

  if (task_handle != NULL) osal_kthread_set_priority(task_handle, TASK_PRIO);
  printf("智能小车演示入口已创建\r\n");
  osal_kthread_unlock();
}

app_run(robot_demo_entry);