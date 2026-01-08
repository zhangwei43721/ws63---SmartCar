/**
 ****************************************************************************************************
 * @file        bsp_body.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS
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
 * 传感器型号:GH719微波传感器
 * 实验现象：人体检测功能
 *
 ****************************************************************************************************
 */
#include "bsp_body.h"

#define BODY_GPIO 1

#define BODY_TASK_STACK_SIZE 0x1000
#define BODY_TASK_PRIO 17

static bool g_bodyState = false;

// 检测到有人经过触发中断执行此函数
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    
    g_bodyState = true;
    //printf("有人经过,状态为%d\r\n",g_bodyState);
}

void body_init(void)
{
    uapi_pin_set_mode(BODY_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BODY_GPIO, GPIO_DIRECTION_INPUT);
    // 上升沿触发中断
    errcode_t ret = uapi_gpio_register_isr_func(BODY_GPIO, GPIO_INTERRUPT_HIGH, gpio_callback_func);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BODY_GPIO);
    }

}

bool getBodyState(void)
{
    return g_bodyState;
}

void setBodayState(bool state)
{
    g_bodyState = state;
}