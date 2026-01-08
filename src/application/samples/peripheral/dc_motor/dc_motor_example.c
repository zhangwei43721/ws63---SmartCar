/**
 ****************************************************************************************************
 * @file        dc_motor_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS LED
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:Hi3863
 * 在线视频:
 * 公司网址:
 * 购买地址:
 *
 ****************************************************************************************************
 * 实验现象：电机转3s停止
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "app_init.h"

#define BLINKY_TASK_STACK_SIZE 0x1000
#define BLINKY_TASK_PRIO 24

//管脚定义
#define DC_MOTOR_PIN         14

// 设置GPIO输出高低电平
#define DC_MOTOR(a)          uapi_gpio_set_val(DC_MOTOR_PIN,a)

static void *dc_motor_task(const char *arg)
{
    unused(arg);

    printf("task start\n");
    // 设置引脚复用模式--HAL_PIO_FUNC_GPIO表示GPIO模式
    uapi_pin_set_mode(DC_MOTOR_PIN, HAL_PIO_FUNC_GPIO);
    // 设置GPIO输出方向
    uapi_gpio_set_dir(DC_MOTOR_PIN, GPIO_DIRECTION_OUTPUT);
    
    DC_MOTOR(GPIO_LEVEL_HIGH);
    osal_msleep(3000); // 3s
    DC_MOTOR(GPIO_LEVEL_LOW);

    while (1)
    {
        osal_msleep(10);
    }
    return NULL;
}

static void dc_motor_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务
    taskid = osal_kthread_create((osal_kthread_handler)dc_motor_task, NULL, "dc_motor_task", BLINKY_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, BLINKY_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(dc_motor_entry);