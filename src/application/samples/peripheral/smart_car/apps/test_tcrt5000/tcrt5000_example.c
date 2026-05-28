/**
 ****************************************************************************************************
 * @file        tcrt5000_example.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       LiteOS TCRT5000红外循迹传感器示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：持续读取左右两侧红外传感器状态并打印到串口
 *
 ****************************************************************************************************
 */

#include <stdio.h>

#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "../../drivers/tcrt5000/bsp_tcrt5000.h"

#define TCRT5000_TASK_STACK_SIZE 0x1000 // 循迹传感器任务栈大小
#define TCRT5000_TASK_PRIO 24           // 循迹传感器任务优先级
#define TCRT5000_DELAY_MS 100           // 采样间隔(ms)

/**
 * @brief TCRT5000红外循迹传感器任务
 * @param arg 任务参数
 * @return 0
 */
static int tcrt5000_task(void *arg)
{
    UNUSED(arg);

    printf("TCRT5000 infrared line tracking sensor task start (ADC mode)\n");

    // 初始化红外传感器ADC模式
    tcrt5000_adc_init();

    printf("TCRT5000 ADC initialized. Reading sensors...\n");

    while (1) {
        tcrt5000_sample();

        // 打印三路模拟量值（ADC电压值 0-3600mV）
        printf("TCRT5000 ADC: L=%4d, M=%4d, R=%4d mV\n", tcrt5000_get_left_adc(), tcrt5000_get_middle_adc(),
               tcrt5000_get_right_adc());

        osal_msleep(TCRT5000_DELAY_MS);
    }

    return 0;
}

/**
 * @brief TCRT5000红外循迹传感器示例入口
 * @return 无
 */
static void tcrt5000_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("TCRT5000 infrared line tracking sensor example entry (ADC mode)\n");

    // 创建任务
    osal_kthread_lock();
    task_handle =
        osal_kthread_create((osal_kthread_handler)tcrt5000_task, NULL, "tcrt5000_adc_task", TCRT5000_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, TCRT5000_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("TCRT5000: Failed to set task priority\n");
        }
    } else {
        printf("TCRT5000: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the tcrt5000_entry. */
app_run(tcrt5000_entry);
