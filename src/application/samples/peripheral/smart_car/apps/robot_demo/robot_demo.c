/**
 ****************************************************************************************************
 * @file        robot_demo.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       LiteOS 智能小车循迹避障示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：智能小车支持三种模式切换：停止、循迹、避障
 *          通过按键切换模式：
 *          - 第一次按键：循迹模式
 *          - 第二次按键：避障模式
 *          - 第三次按键：停止模式
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "watchdog.h"
#include "common_def.h"
#include "soc_osal.h"
#include "osal_timer.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "app_init.h"
#include "core/robot_config.h"
#include "core/robot_mgr.h"

/* 任务配置 */
#define ROBOT_DEMO_TASK_STACK_SIZE CAR_CONTROL_DEMO_TASK_STACK_SIZE
#define ROBOT_DEMO_TASK_PRIO CAR_CONTROL_DEMO_TASK_PRIORITY

/* GPIO 配置（KEY1，用于模式切换） */
#define ROBOT_MODE_SWITCH_GPIO 3

static unsigned long long g_mode_switch_tick = 0;

/**
 * @brief 按键中断响应函数
 * @note  处理状态切换逻辑：停止 -> 循迹 -> 避障 -> WiFi -> 停止
 */
static void mode_switch_isr(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    unsigned long long current_tick = 0;
    unsigned long long tick_interval = 0;
    unsigned long protect_ticks;
    CarStatus current_status;
    CarStatus next_status = CAR_STOP_STATUS; // 默认下一个状态

    // 1. 获取当前时间滴答
    current_tick = osal_get_jiffies();
    tick_interval = current_tick - g_mode_switch_tick;
    protect_ticks = osal_msecs_to_jiffies(KEY_INTERRUPT_PROTECT_TIME);

    // 2. 按键防抖处理
    if (tick_interval < protect_ticks) {
        return;
    }
    g_mode_switch_tick = current_tick;

    // 3. 获取当前状态并决定下一状态 (无宏定义，固定逻辑)
    current_status = robot_mgr_get_status();

    switch (current_status) {
        case CAR_WIFI_CONTROL_STATUS:
            printf("切换到 [停止] 模式\r\n");
            next_status = CAR_STOP_STATUS;
            break;

        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            printf("切换到 [WiFi 遥控] 模式\r\n");
            next_status = CAR_WIFI_CONTROL_STATUS;
            break;

        case CAR_STOP_STATUS:
            printf("切换到 [循迹] 模式\r\n");
            next_status = CAR_TRACE_STATUS;
            break;

        case CAR_TRACE_STATUS:
            printf("切换到 [避障] 模式\r\n");
            next_status = CAR_OBSTACLE_AVOIDANCE_STATUS;
            break;

        default:
            printf("未知状态，重置为 [停止]\r\n");
            next_status = CAR_STOP_STATUS;
            break;
    }

    // 4. 执行状态切换
    robot_mgr_set_status(next_status);
}

/**
 * @brief 初始化模式切换按键 GPIO
 */
static void mode_switch_init(void)
{
    // 参考按键示例：配置为 GPIO 功能，上拉输入
    uapi_pin_set_mode(ROBOT_MODE_SWITCH_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(ROBOT_MODE_SWITCH_GPIO, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(ROBOT_MODE_SWITCH_GPIO, PIN_PULL_TYPE_UP);
}

/**
 * @brief 注册按键中断
 */
static void interrupt_monitor(void)
{
    // 注册下降沿中断 (按下触发)
    errcode_t ret = uapi_gpio_register_isr_func(ROBOT_MODE_SWITCH_GPIO, GPIO_INTERRUPT_FALLING_EDGE, mode_switch_isr);
    if (ret != ERRCODE_SUCC) {
        printf("注册模式切换中断失败, ret=%d\r\n", ret);
    }
}

/**
 * @brief 智能小车主控制任务
 */
static void *robot_demo_task(const char *arg)
{
    UNUSED(arg);

    printf("智能小车演示任务启动 (WiFi 已启用，无蓝牙)\r\n");

    // 1. 初始化底层驱动和控制逻辑
    robot_mgr_init();

    // 2. 初始化按键中断
    mode_switch_init();
    interrupt_monitor();

    printf("==========================================\r\n");
    printf("智能小车系统已初始化。默认: 待机模式\r\n");
    printf("模式切换循环 (按下 KEY1):\r\n");
    printf("  [1] 循迹模式 (Tracking)\r\n");
    printf("  [2] 避障模式\r\n");
    printf("  [3] WiFi 遥控模式\r\n");
    printf("  [4] 停止模式 (待机)\r\n");
    printf("==========================================\r\n");

    // 3. 主循环
    while (1) {
        robot_mgr_tick();
        uapi_watchdog_kick();

        // 短暂延时，让出 CPU 时间片，避免任务饿死其他线程
        osal_msleep(20);
    }

    return NULL;
}

/**
 * @brief 任务入口函数
 */
static void robot_demo_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("智能小车演示入口已创建\r\n");

    // 创建 OSAL 线程
    osal_kthread_lock();
    task_handle =
        osal_kthread_create((osal_kthread_handler)robot_demo_task, NULL, "robot_demo_task", ROBOT_DEMO_TASK_STACK_SIZE);

    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, ROBOT_DEMO_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("智能小车演示: 设置任务优先级失败\r\n");
        }
    } else
        printf("智能小车演示: 创建任务失败\r\n");

    osal_kthread_unlock();
}

/* 注册应用入口 */
app_run(robot_demo_entry);