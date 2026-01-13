/**
 ****************************************************************************************************
 * @file        bsp_tcrt5000.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       TCRT5000红外循迹传感器BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：TCRT5000红外循迹传感器检测黑线
 *
 ****************************************************************************************************
 */

#include "bsp_tcrt5000.h"
#include "gpio.h"
#include "pinctrl.h"
#include "hal_gpio.h"
#include "soc_osal.h"

/**
 * @brief 初始化TCRT5000红外循迹传感器
 * @return 无
 */
void tcrt5000_init(void)
{
    // 初始化左侧传感器为输入，上拉
    uapi_pin_set_pull(TCRT5000_LEFT_GPIO, PIN_PULL_TYPE_UP);
    uapi_gpio_set_dir(TCRT5000_LEFT_GPIO, GPIO_DIRECTION_INPUT);

    // 初始化中间传感器为输入，上拉
    uapi_pin_set_pull(TCRT5000_MIDDLE_GPIO, PIN_PULL_TYPE_UP);
    uapi_gpio_set_dir(TCRT5000_MIDDLE_GPIO, GPIO_DIRECTION_INPUT);

    // 初始化右侧传感器为输入，上拉
    uapi_pin_set_pull(TCRT5000_RIGHT_GPIO, PIN_PULL_TYPE_UP);
    uapi_gpio_set_dir(TCRT5000_RIGHT_GPIO, GPIO_DIRECTION_INPUT);
}

/**
 * @brief 获取左侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_left(void)
{
    return (unsigned int)uapi_gpio_get_val(TCRT5000_LEFT_GPIO);
}

/**
 * @brief 获取中间传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_middle(void)
{
    return (unsigned int)uapi_gpio_get_val(TCRT5000_MIDDLE_GPIO);
}

/**
 * @brief 获取右侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_right(void)
{
    return (unsigned int)uapi_gpio_get_val(TCRT5000_RIGHT_GPIO);
}
