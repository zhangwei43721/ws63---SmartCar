/**
 ****************************************************************************************************
 * @file        sg90_example.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       LiteOS SG90舵机示例 (智能小车专用)
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：SG90舵机从0度转到180度，再从180度转回0度，循环往复
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "app_init.h"
#include "watchdog.h"
#include "../../drivers/sg90/bsp_sg90.h"

#define SG90_TASK_STACK_SIZE 0x1000
#define SG90_TASK_PRIO 24
#define SG90_DELAY_MS 1   // 舵机转动延时
#define SG90_ANGLE_STEP 2 // 每次转动的角度步长

/**
 * @brief SG90舵机任务
 * @param arg 任务参数
 * @return NULL
 */
static void *sg90_task(const char *arg)
{
    UNUSED(arg);
    int angle = SG90_ANGLE_MIN;
    int step = SG90_ANGLE_STEP;

    printf("SG90 servo task start\n");
    sg90_init();

    while (1) {
        // 设置目标角度，后台守护任务会持续发送PWM波形
        sg90_set_angle((unsigned int)angle);
        
        // 由于后台守护任务会持续发送PWM波形，这里只需要延时即可
        osal_msleep(20);  // 小延时让舵机有时间反应

        // 更新角度逻辑
        angle += step;

        if (angle >= SG90_ANGLE_MAX) {
            angle = SG90_ANGLE_MAX;
            step = -SG90_ANGLE_STEP;
            osal_msleep(500); // 到达边缘停顿 0.5s
        } else if (angle <= SG90_ANGLE_MIN) {
            angle = SG90_ANGLE_MIN;
            step = SG90_ANGLE_STEP;
            osal_msleep(500); // 到达边缘停顿 0.5s
        }
    }
    return NULL;
}

/**
 * @brief SG90舵机示例入口
 * @return 无
 */
static void sg90_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("SG90 servo example entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)sg90_task, NULL, "sg90_task", SG90_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, SG90_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("SG90: Failed to set task priority\n");
        }
    } else {
        printf("SG90: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the sg90_entry. */
app_run(sg90_entry);
