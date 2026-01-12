/**
 ****************************************************************************************************
 * @file        hcsr04_example.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       LiteOS HC-SR04超声波测距传感器示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：持续测量前方障碍物距离并打印到串口
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "app_init.h"
#include "bsp_include/bsp_hcsr04.h"

#define HCSR04_TASK_STACK_SIZE 0x1000
#define HCSR04_TASK_PRIO 24
#define HCSR04_DELAY_MS 200

/**
 * @brief HC-SR04超声波测距任务
 * @param arg 任务参数
 * @return NULL
 */
static void *hcsr04_task(const char *arg)
{
    UNUSED(arg);
    float distance = 0.0;

    printf("HC-SR04 ultrasonic sensor task start\n");

    // 初始化超声波传感器
    hcsr04_init();

    while (1) {
        // 获取距离测量值
        distance = hcsr04_get_distance();

        if (distance > 0) {
            printf("Distance: %.2f cm\n", distance);
        } else {
            printf("Distance measurement failed\n");
        }

        osal_msleep(HCSR04_DELAY_MS);
    }

    return NULL;
}

/**
 * @brief HC-SR04超声波示例入口
 * @return 无
 */
static void hcsr04_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("HC-SR04 ultrasonic sensor example entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)hcsr04_task, NULL, "hcsr04_task", HCSR04_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, HCSR04_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("HC-SR04: Failed to set task priority\n");
        }
    } else {
        printf("HC-SR04: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the hcsr04_entry. */
app_run(hcsr04_entry);
