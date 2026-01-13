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

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "app_init.h"
#include "../../drivers/tcrt5000/bsp_tcrt5000.h"

#define TCRT5000_TASK_STACK_SIZE 0x1000
#define TCRT5000_TASK_PRIO 24
#define TCRT5000_DELAY_MS 100

/**
 * @brief TCRT5000红外循迹传感器任务
 * @param arg 任务参数
 * @return NULL
 */
static void *tcrt5000_task(const char *arg)
{
    UNUSED(arg);
    unsigned int left = 0;
    unsigned int middle = 0;
    unsigned int right = 0;

    printf("TCRT5000 infrared line tracking sensor task start\n");

    // 初始化红外传感器
    tcrt5000_init();

    while (1) {
        // 获取三个传感器状态
        left = tcrt5000_get_left();
        middle = tcrt5000_get_middle();
        right = tcrt5000_get_right();

        // 显示传感器状态
        printf("Sensors: L=%s M=%s R=%s\n",
               left == TCRT5000_ON_BLACK ? "BLACK" : "WHITE",
               middle == TCRT5000_ON_BLACK ? "BLACK" : "WHITE",
               right == TCRT5000_ON_BLACK ? "BLACK" : "WHITE");

        osal_msleep(TCRT5000_DELAY_MS);
    }

    return NULL;
}

/**
 * @brief TCRT5000红外循迹传感器示例入口
 * @return 无
 */
static void tcrt5000_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("TCRT5000 infrared line tracking sensor example entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)tcrt5000_task, NULL, "tcrt5000_task", TCRT5000_TASK_STACK_SIZE);
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
