/**
 ****************************************************************************************************
 * @file        GH719_example.c
 * @author      SkyForever
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
 * 硬件配置说明:
 * 请务必将模块背面的焊盘修改为 [模式 01]:
 * H5 -> 不接
 * H4 -> 接 1
 * 效果: 离开后 2秒 立即关灯
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "app_init.h"

#define BSP_LED 11   // 蓝色LED
#define GH719_GPIO 1 // 传感器输入

#define GH719_TASK_STACK_SIZE 0x1000
#define GH719_TASK_PRIO 17

void led_init(void)
{
    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
}

void GH719_init(void)
{
    uapi_pin_set_mode(GH719_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(GH719_GPIO, GPIO_DIRECTION_INPUT);
}

static void *GH719_task(const char *arg)
{
    UNUSED(arg);
    led_init();
    GH719_init();

    printf("[日志] GH719 检测任务启动。\n");

    int last_status = -1;
    int current_status = 0;

    while (1) {
        uapi_watchdog_kick(); // 喂狗

        // 1. 获取状态
        current_status = uapi_gpio_get_val(GH719_GPIO);

        // 2. 状态处理
        if (current_status != last_status) {
            if (current_status == 1) {
                // === 变高：有人 ===
                uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_HIGH);
                printf("[日志] GH719 状态: 被覆盖 (Light ON)\n");
                last_status = 1;
            } else {
                // === 变低：人走了 ===
                uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
                printf("[日志] GH719 状态: 未被覆盖 (Light OFF)\n");

                osal_msleep(500);

                last_status = 0;
            }
        }

        // 3. 这里的延时是常规轮询延时
        osal_msleep(10);
    }

    return NULL;
}

static void GH719_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    osal_kthread_lock();
    taskid = osal_kthread_create((osal_kthread_handler)GH719_task, NULL, "GH719_task", GH719_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, GH719_TASK_PRIO);
    if (ret != OSAL_SUCCESS)
        printf("create task failed .\n");
    osal_kthread_unlock();
}

app_run(GH719_entry);