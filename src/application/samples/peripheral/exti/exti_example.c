/**
 ****************************************************************************************************
 * @file        exti_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS exti
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
 * 实验现象：按键KEY1控制LED开，KEY2键控制LED关
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
#define BUTTON1_GPIO 11 // 按键1
#define BUTTON2_GPIO 12 // 按键2

#define EXTI_TASK_STACK_SIZE 0x1000
#define EXTI_TASK_PRIO 17

// 触发中断执行此函数
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);

    static uint8_t key1 = 0;
    osal_msleep(10); // 消抖
    key1 = uapi_gpio_get_val(BUTTON1_GPIO);
    if(key1 == 0)
        uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_HIGH);
}

static void gpio_callback_func2(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);

    static uint8_t key2 = 0;
    osal_msleep(10);//消抖
    key2 = uapi_gpio_get_val(BUTTON2_GPIO);
    if(key2 == 0)
        uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
}


void led_init(void)
{
    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
    
}

void key_init(void)
{
    uapi_pin_set_mode(BUTTON1_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BUTTON1_GPIO, GPIO_DIRECTION_INPUT);
    // 下降沿触发中断
    errcode_t ret = uapi_gpio_register_isr_func(BUTTON1_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BUTTON1_GPIO);
    }

    uapi_pin_set_mode(BUTTON2_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BUTTON2_GPIO, GPIO_DIRECTION_INPUT);
    // 下降沿触发中断
    ret = uapi_gpio_register_isr_func(BUTTON2_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func2);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BUTTON2_GPIO);
    }

}

static void *exti_task(const char *arg)
{
    UNUSED(arg);
    led_init(); // led初始化
    key_init(); // 按键初始化

    while (1) 
    {
        osal_msleep(10);
    }

    return NULL;
}

static void exti_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务1
    taskid = osal_kthread_create((osal_kthread_handler)exti_task, NULL, "exti_task", EXTI_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, EXTI_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(exti_entry);