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
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "app_init.h"
#include "bsp_include/bsp_robot_control.h"

#define ROBOT_DEMO_TASK_STACK_SIZE CAR_CONTROL_DEMO_TASK_STACK_SIZE
#define ROBOT_DEMO_TASK_PRIO CAR_CONTROL_DEMO_TASK_PRIORITY

// 模式切换按键定义
// 使用 JP5 排母上的 GPIO_03 (原 KEY1)
// 注意: GPIO_00-02 已被红外循迹传感器占用
#define ROBOT_MODE_SWITCH_GPIO 3
#define ROBOT_GPIO_FUNC 0

static unsigned int g_mode_switch_tick = 0;

/**
 * @brief 按键中断响应函数
 * @param arg 参数
 * @return NULL
 */
static void *mode_switch_isr(const char *arg)
{
    UNUSED(arg);
    unsigned int tick_interval = 0;
    unsigned int current_tick = 0;
    CarStatus current_status;

    current_tick = osal_get_tick();
    tick_interval = current_tick - g_mode_switch_tick;

    // 按键防抖
    if (tick_interval < KEY_INTERRUPT_PROTECT_TIME) {
        return NULL;
    }
    g_mode_switch_tick = current_tick;

    current_status = robot_get_status();

    if (current_status == CAR_STOP_STATUS) {
        printf("Switch to TRACE mode\n");
        robot_set_status(CAR_TRACE_STATUS);
    } else if (current_status == CAR_TRACE_STATUS) {
        printf("Switch to OBSTACLE AVOIDANCE mode\n");
        robot_set_status(CAR_OBSTACLE_AVOIDANCE_STATUS);
    } else if (current_status == CAR_OBSTACLE_AVOIDANCE_STATUS) {
        printf("Switch to STOP mode\n");
        robot_set_status(CAR_STOP_STATUS);
    }

    return NULL;
}

/**
 * @brief 初始化模式切换按键
 * @return 无
 */
static void mode_switch_init(void)
{
    uapi_pin_set_pull(ROBOT_MODE_SWITCH_GPIO, PIN_PULL_TYPE_UP);
    uapi_gpio_set_dir(ROBOT_MODE_SWITCH_GPIO, GPIO_DIRECTION_INPUT);
}

/**
 * @brief 注册按键中断
 * @return 无
 */
static void interrupt_monitor(void)
{
    uapi_gpio_register_isr_fn(ROBOT_MODE_SWITCH_GPIO, GPIO_INT_TRIGGER_MODE_FALLING_EDGE,
                              (osal_irq_handler)mode_switch_isr, NULL);
}

/**
 * @brief 智能小车主控制任务
 * @param arg 任务参数
 * @return NULL
 */
static void *robot_demo_task(const char *arg)
{
    UNUSED(arg);
    CarStatus status;

    printf("Robot demo task start\n");

    // 初始化智能小车控制系统
    robot_control_init();

    // 初始化模式切换按键
    mode_switch_init();
    interrupt_monitor();

    printf("Robot demo initialized. Current mode: STOP\n");
    printf("Press the mode switch button to change mode:\n");
    printf("  1st press: TRACE mode\n");
    printf("  2nd press: OBSTACLE AVOIDANCE mode\n");
    printf("  3rd press: STOP mode\n");

    while (1) {
        status = robot_get_status();

        switch (status) {
            case CAR_STOP_STATUS:
                car_stop();
                osal_msleep(100);
                break;

            case CAR_OBSTACLE_AVOIDANCE_STATUS:
                printf("Entering OBSTACLE AVOIDANCE mode\n");
                robot_obstacle_avoidance_mode();
                printf("Exiting OBSTACLE AVOIDANCE mode\n");
                break;

            case CAR_TRACE_STATUS:
                printf("Entering TRACE mode\n");
                robot_trace_mode();
                printf("Exiting TRACE mode\n");
                break;

            default:
                break;
        }

        osal_msleep(20);
    }

    return NULL;
}

/**
 * @brief 智能小车示例入口
 * @return 无
 */
static void robot_demo_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("Robot demo entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)robot_demo_task, NULL,
                                      "robot_demo_task", ROBOT_DEMO_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, ROBOT_DEMO_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("Robot demo: Failed to set task priority\n");
        }
    } else {
        printf("Robot demo: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the robot_demo_entry. */
app_run(robot_demo_entry);
