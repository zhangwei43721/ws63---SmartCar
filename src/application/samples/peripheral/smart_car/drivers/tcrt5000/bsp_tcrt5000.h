/**
 ****************************************************************************************************
 * @file        bsp_tcrt5000.h
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       TCRT5000红外循迹传感器BSP层头文件
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

#ifndef __BSP_TCRT5000_H__
#define __BSP_TCRT5000_H__

#include <stdint.h>

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

// TCRT5000红外循迹传感器引脚定义
#define TCRT5000_LEFT_GPIO 12    // 左侧传感器
#define TCRT5000_MIDDLE_GPIO 10  // 中间传感器
#define TCRT5000_RIGHT_GPIO 9    // 右侧传感器

// ADC通道定义 (各GPIO对应的ADC通道)
#define TCRT5000_LEFT_ADC_CHANNEL 5    // GPIO12 -> ADC通道5
#define TCRT5000_MIDDLE_ADC_CHANNEL 3  // GPIO10 -> ADC通道3
#define TCRT5000_RIGHT_ADC_CHANNEL 2   // GPIO9  -> ADC通道2

// 传感器状态定义
#define TCRT5000_ON_BLACK 0  // 检测到黑线
#define TCRT5000_ON_WHITE 1  // 检测到白色/无黑线

// ADC阈值定义（mV）
// ADC值 >= 阈值表示检测到黑线，ADC值 < 阈值表示检测到白线
#define TCRT5000_LEFT_THRESHOLD 2000    // 左侧传感器阈值
#define TCRT5000_MIDDLE_THRESHOLD 1900  // 中间传感器阈值
#define TCRT5000_RIGHT_THRESHOLD 1900   // 右侧传感器阈值

// ADC数据存储（使用自动扫描模式）
extern uint32_t
    g_tcrt5000_adc_data[3];  // 存储左、中、右三个传感器的ADC电压值（mV）

/**
 * @brief ADC自动扫描回调函数
 * @param channel ADC通道
 * @param buffer ADC采样数据缓冲区
 * @param length 数据长度
 * @param next 继续扫描标志
 */
void tcrt5000_adc_callback(uint8_t channel, uint32_t* buffer, uint32_t length,
                           bool* next);

/**
 * @brief 初始化TCRT5000红外循迹传感器
 * @return 无
 */
void tcrt5000_init(void);

/**
 * @brief 获取左侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_left(void);

/**
 * @brief 获取中间传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_middle(void);

/**
 * @brief 获取右侧传感器状态
 * @return 0: 检测到黑线, 1: 未检测到黑线
 */
unsigned int tcrt5000_get_right(void);

/**
 * @brief 初始化TCRT5000 ADC模式
 * @return 无
 */
void tcrt5000_adc_init(void);

/**
 * @brief 获取左侧传感器ADC电压值
 * @return ADC电压值 (mV)
 */
uint32_t tcrt5000_get_left_adc(void);

/**
 * @brief 获取中间传感器ADC电压值
 * @return ADC电压值 (mV)
 */
uint32_t tcrt5000_get_middle_adc(void);

/**
 * @brief 获取右侧传感器ADC电压值
 * @return ADC电压值 (mV)
 */
uint32_t tcrt5000_get_right_adc(void);

#endif /* __BSP_TCRT5000_H__ */
