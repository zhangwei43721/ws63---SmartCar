/**
 ****************************************************************************************************
 * @file        led_example.c
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
 * 实验现象：点亮LED
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
#define BSP_LED 14                  // RED
#define CONFIG_BLINKY_DURATION_50MS 500

static void *led_task(const char *arg)
{
    unused(arg);

    printf("task start\n");

    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);

    while (1) {
        osal_msleep(CONFIG_BLINKY_DURATION_50MS);
        uapi_gpio_toggle(BSP_LED);
        osal_printk("task start\r\n");
    }
    return NULL;
}

static void led_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务
    taskid = osal_kthread_create((osal_kthread_handler)led_task, NULL, "led_task", BLINKY_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, BLINKY_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(led_entry);