/**
 ****************************************************************************************************
 * @file        l9110s_example.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       LiteOS L9110S电机驱动示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：小车依次执行前进、后退、左转、右转、停止动作
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "app_init.h"
#include "../../drivers/l9110s/bsp_l9110s.h"

#define L9110S_TASK_STACK_SIZE 0x1000
#define L9110S_TASK_PRIO 24
#define L9110S_DELAY_MS 1000

/**
 * @brief L9110S电机测试任务
 * @param arg 任务参数
 * @return 0
 */
static int l9110s_task(void *arg)
{
    UNUSED(arg);

    printf("L9110S motor test task start\n");

    // 初始化电机驱动
    l9110s_init();

    while (1) {
        // // 前进
        // printf("Car moving forward\n");
        // l9110s_set_differential(100, 100);
        // osal_msleep(L9110S_DELAY_MS);

        // // 后退
        // printf("Car moving backward\n");
        // l9110s_set_differential(-100, -100);
        // osal_msleep(L9110S_DELAY_MS);

        // // 左转
        // printf("Car turning left\n");
        // l9110s_set_differential(0, 100);
        // osal_msleep(L9110S_DELAY_MS);

        // // 右转
        // printf("Car turning right\n");
        // l9110s_set_differential(100, 0);
        // osal_msleep(L9110S_DELAY_MS);

        // 停止
        printf("Car stopping\n");
        l9110s_set_differential(0, 0);
        osal_msleep(L9110S_DELAY_MS);
    }

    return 0;
}

/**
 * @brief L9110S电机示例入口
 * @return 无
 */
static void l9110s_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("L9110S motor example entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)l9110s_task, NULL, "l9110s_task", L9110S_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, L9110S_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("L9110S: Failed to set task priority\n");
        }
    } else {
        printf("L9110S: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the l9110s_entry. */
app_run(l9110s_entry);
