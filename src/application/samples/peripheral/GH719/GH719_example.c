/**
 ****************************************************************************************************
 * @file        GH719_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-12-27
 * @brief       LiteOS GH719
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
 * 实验现象：人体功能检测
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "app_init.h"

#define BSP_LED 7      // RED
#define GH719_GPIO 1 // GH719

#define GREEN_LED 11      // GREEN
#define GH719_TASK_STACK_SIZE 0x1000
#define GH719_TASK_PRIO 17

void led_init(void)
{
    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);

    uapi_pin_set_mode(GREEN_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(GREEN_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(GREEN_LED, GPIO_LEVEL_LOW);
}

void GH719_init(void)
{
    uapi_pin_set_mode(GH719_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(GH719_GPIO, GPIO_DIRECTION_INPUT);
}

static void *GH719_task(const char *arg)
{
    UNUSED(arg);
    //led_init(); // led初始化
    GH719_init(); // GH719初始化
    printf("GH719 init success.\n");
    while (1) {
        uapi_watchdog_kick(); // 喂狗，防止程序出现异常系统挂死

        if(1 == uapi_gpio_get_val(GH719_GPIO))
        {
            printf("Human body detection successful.\n");
        }
        
        osal_msleep(100);
    }

    return NULL;
}

static void GH719_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务1
    taskid = osal_kthread_create((osal_kthread_handler)GH719_task, NULL, "GH719_task", GH719_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, GH719_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(GH719_entry);