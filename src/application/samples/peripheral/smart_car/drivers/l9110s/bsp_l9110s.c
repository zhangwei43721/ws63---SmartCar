/**
 ****************************************************************************************************
 * @file        bsp_l9110s.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       L9110S电机驱动BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：L9110S电机控制，实现小车前进、后退、左转、右转、停止
 *
 ****************************************************************************************************
 */

#include "bsp_l9110s.h"
#include "gpio.h"
#include "pinctrl.h"
#include "hal_gpio.h"
#include "soc_osal.h"

/**
 * @brief GPIO控制函数
 * @param gpio GPIO引脚号
 * @param value 输出电平值
 * @return 无
 */
static void gpio_control(unsigned int gpio, unsigned int value)
{
    uapi_gpio_set_dir(gpio, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(gpio, value);
}

/**
 * @brief 初始化L9110S电机驱动
 * @return 无
 */
void l9110s_init(void)
{
    // 初始化左轮电机控制引脚为输出
    uapi_gpio_set_dir(L9110S_LEFT_A_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(L9110S_LEFT_B_GPIO, GPIO_DIRECTION_OUTPUT);

    // 初始化右轮电机控制引脚为输出
    uapi_gpio_set_dir(L9110S_RIGHT_A_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(L9110S_RIGHT_B_GPIO, GPIO_DIRECTION_OUTPUT);

    // 初始状态停止
    car_stop();
}

/**
 * @brief 小车前进
 * 左轮正转: LEFT_A=1, LEFT_B=0
 * 右轮正转: RIGHT_A=1, RIGHT_B=0
 * @return 无
 */
void car_forward(void)
{
    gpio_control(L9110S_LEFT_A_GPIO, 1);
    gpio_control(L9110S_LEFT_B_GPIO, 0);
    gpio_control(L9110S_RIGHT_A_GPIO, 1);
    gpio_control(L9110S_RIGHT_B_GPIO, 0);
}

/**
 * @brief 小车后退
 * 左轮反转: LEFT_A=0, LEFT_B=1
 * 右轮反转: RIGHT_A=0, RIGHT_B=1
 * @return 无
 */
void car_backward(void)
{
    gpio_control(L9110S_LEFT_A_GPIO, 0);
    gpio_control(L9110S_LEFT_B_GPIO, 1);
    gpio_control(L9110S_RIGHT_A_GPIO, 0);
    gpio_control(L9110S_RIGHT_B_GPIO, 1);
}

/**
 * @brief 小车左转
 * 左轮停止: LEFT_A=0, LEFT_B=0
 * 右轮正转: RIGHT_A=1, RIGHT_B=0
 * @return 无
 */
void car_left(void)
{
    gpio_control(L9110S_LEFT_A_GPIO, 0);
    gpio_control(L9110S_LEFT_B_GPIO, 0);
    gpio_control(L9110S_RIGHT_A_GPIO, 1);
    gpio_control(L9110S_RIGHT_B_GPIO, 0);
}

/**
 * @brief 小车右转
 * 左轮正转: LEFT_A=1, LEFT_B=0
 * 右轮停止: RIGHT_A=0, RIGHT_B=0
 * @return 无
 */
void car_right(void)
{
    gpio_control(L9110S_LEFT_A_GPIO, 1);
    gpio_control(L9110S_LEFT_B_GPIO, 0);
    gpio_control(L9110S_RIGHT_A_GPIO, 0);
    gpio_control(L9110S_RIGHT_B_GPIO, 0);
}

/**
 * @brief 小车停止
 * 左轮刹车: LEFT_A=1, LEFT_B=1
 * 右轮刹车: RIGHT_A=1, RIGHT_B=1
 * @return 无
 */
void car_stop(void)
{
    gpio_control(L9110S_LEFT_A_GPIO, 1);
    gpio_control(L9110S_LEFT_B_GPIO, 1);
    gpio_control(L9110S_RIGHT_A_GPIO, 1);
    gpio_control(L9110S_RIGHT_B_GPIO, 1);
}
