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
#include "adc.h"
#include <stdio.h>
#include <stdbool.h>

// ADC数据存储（使用自动扫描模式）
uint32_t g_tcrt5000_adc_data[3] = {0};  // 存储左、中、右三个传感器的ADC电压值（mV）

/**
 * @brief ADC自动扫描回调函数
 * @param channel ADC通道
 * @param buffer ADC采样数据缓冲区
 * @param length 数据长度
 * @param next 继续扫描标志
 */
void tcrt5000_adc_callback(uint8_t channel, uint32_t *buffer, uint32_t length, bool *next)
{
    if (length > 0 && buffer != NULL) {
        // 根据通道号存储数据
        // 通道2 -> 左侧传感器 (索引0)
        // 通道1 -> 中间传感器 (索引1)
        // 通道0 -> 右侧传感器 (索引2)
        if (channel == TCRT5000_LEFT_ADC_CHANNEL) {
            g_tcrt5000_adc_data[0] = buffer[0];
        } else if (channel == TCRT5000_MIDDLE_ADC_CHANNEL) {
            g_tcrt5000_adc_data[1] = buffer[0];
        } else if (channel == TCRT5000_RIGHT_ADC_CHANNEL) {
            g_tcrt5000_adc_data[2] = buffer[0];
        }
    }
    *next = false;  // 停止扫描
}

/**
 * @brief 初始化TCRT5000红外循迹传感器
 * @return 无
 */
void tcrt5000_init(void)
{
    // 初始化左侧传感器（GPIO_04，需要设置复用功能2）
    uapi_pin_set_mode(TCRT5000_LEFT_GPIO, PIN_MODE_2);
    uapi_pin_set_pull(TCRT5000_LEFT_GPIO, PIN_PULL_TYPE_DISABLE);
    uapi_gpio_set_dir(TCRT5000_LEFT_GPIO, GPIO_DIRECTION_INPUT);

    // 初始化中间传感器为输入，禁用内部上下拉（使用外部上拉）
    uapi_pin_set_mode(TCRT5000_MIDDLE_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_MIDDLE_GPIO, PIN_PULL_TYPE_DISABLE);
    uapi_gpio_set_dir(TCRT5000_MIDDLE_GPIO, GPIO_DIRECTION_INPUT);

    // 初始化右侧传感器为输入，禁用内部上下拉（使用外部上拉）
    uapi_pin_set_mode(TCRT5000_RIGHT_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_RIGHT_GPIO, PIN_PULL_TYPE_DISABLE);
    uapi_gpio_set_dir(TCRT5000_RIGHT_GPIO, GPIO_DIRECTION_INPUT);
}

/**
 * @brief 获取左侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_left(void)
{
    return (g_tcrt5000_adc_data[0] >= TCRT5000_LEFT_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
}

/**
 * @brief 获取中间传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_middle(void)
{
    return (g_tcrt5000_adc_data[1] >= TCRT5000_MIDDLE_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
}

/**
 * @brief 获取右侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_right(void)
{
    return (g_tcrt5000_adc_data[2] >= TCRT5000_RIGHT_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
}

/**
 * @brief 初始化TCRT5000 ADC模式
 * @return 无
 */
void tcrt5000_adc_init(void)
{
    // 初始化ADC
    uapi_adc_init(ADC_CLOCK_500KHZ);
    // 使能ADC电源
    uapi_adc_power_en(AFE_SCAN_MODE_MAX_NUM, true);

    // 配置GPIO为ADC模式（禁用复用功能和上下拉）
    uapi_pin_set_mode(TCRT5000_LEFT_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_LEFT_GPIO, PIN_PULL_TYPE_DISABLE);

    uapi_pin_set_mode(TCRT5000_MIDDLE_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_MIDDLE_GPIO, PIN_PULL_TYPE_DISABLE);

    uapi_pin_set_mode(TCRT5000_RIGHT_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_RIGHT_GPIO, PIN_PULL_TYPE_DISABLE);
}

/**
 * @brief 获取左侧传感器ADC电压值
 * @return ADC电压值 (mV)
 */
uint32_t tcrt5000_get_left_adc(void)
{
    return g_tcrt5000_adc_data[0];
}

/**
 * @brief 获取中间传感器ADC电压值
 * @return ADC电压值 (mV)
 */
uint32_t tcrt5000_get_middle_adc(void)
{
    return g_tcrt5000_adc_data[1];
}

/**
 * @brief 获取右侧传感器ADC电压值
 * @return ADC电压值 (mV)
 */
uint32_t tcrt5000_get_right_adc(void)
{
    return g_tcrt5000_adc_data[2];
}
