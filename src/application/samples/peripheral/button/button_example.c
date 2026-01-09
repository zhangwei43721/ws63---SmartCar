/**
 ****************************************************************************************************
 * @file        button_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS button
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

#define BSP_LED 14     // LED1
#define BUTTON1_GPIO 3 // 按键1
#define BUTTON2_GPIO 2 // 按键2
#define GREEN_LED 13   // GREEN
#define BUTTON_TASK_STACK_SIZE 0x1000
#define BUTTON_TASK_PRIO 17

// 使用宏定义独立按键按下的键值
#define KEY1_PRESS 1
#define KEY2_PRESS 2
#define KEY_UNPRESS 0

// 触发中断执行此函数
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    // g_ledState = !g_ledState;
    // printf("Button pressed.\r\n");
}

static void gpio_callback_func2(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
}

/*******************************************************************************
* 函 数 名       : key_scan
* 函数功能        : 检测独立按键是否按下，按下则返回对应键值
* 输    入       : mode=0：单次扫描按键
                  mode=1：连续扫描按键
* 输    出       : KEY1_PRESS：K1按下
                  KEY2_PRESS：K2按下
                  KEY_UNPRESS：未有按键按下
*******************************************************************************/
uint8_t key_scan(void)
{
    static uint8_t key1 = 0;
    static uint8_t key2 = 0;

    key1 = uapi_gpio_get_val(BUTTON1_GPIO);
    key2 = uapi_gpio_get_val(BUTTON2_GPIO);

    if (key1 == 0 || key2 == 0) {
        osal_msleep(10); // 消抖
        key1 = uapi_gpio_get_val(BUTTON1_GPIO);
        key2 = uapi_gpio_get_val(BUTTON2_GPIO);
        if (key1 == 0 || key2 == 0) {
            if (key1 == 0)
                return KEY1_PRESS;
            else if (key2 == 0)
                return KEY2_PRESS;
        }
    }

    return KEY_UNPRESS;
}

void led_init(void)
{
    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);

    uapi_pin_set_mode(GREEN_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(GREEN_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(GREEN_LED, GPIO_LEVEL_LOW);
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

static void *button_task(const char *arg)
{
    UNUSED(arg);
    led_init(); // led初始化
    key_init(); // 按键初始化

    while (1) {
        uapi_watchdog_kick(); // 喂狗，防止程序出现异常系统挂死
        // if (g_ledState) {
        //     uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_HIGH);
        // } else {
        //     uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
        // }

        uint8_t key = key_scan(); // 按键扫描
        if (key == KEY1_PRESS)
            uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_HIGH);
        else if (key == KEY2_PRESS)
            uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);

        osal_msleep(10);
    }

    return NULL;
}

static void button_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务1
    taskid = osal_kthread_create((osal_kthread_handler)button_task, NULL, "button_task", BUTTON_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, BUTTON_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(button_entry);