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

#include <stdbool.h>
#include <stdint.h>

// 传感器状态定义
#define TCRT5000_ON_BLACK 0 // 检测到黑线
#define TCRT5000_ON_WHITE 1 // 检测到白色/无黑线

// ADC阈值定义（mV）— 应用层根据阈值判定黑白线
#define TCRT5000_LEFT_THRESHOLD 2000
#define TCRT5000_MIDDLE_THRESHOLD 1900
#define TCRT5000_RIGHT_THRESHOLD 1900

/**
 * @brief 原子读取三路 ADC 当前快照（mV）
 */
void tcrt5000_snapshot(uint32_t *l, uint32_t *m, uint32_t *r);

/**
 * @brief 初始化TCRT5000 ADC模式
 */
void tcrt5000_adc_init(void);

/**
 * @brief 触发一次三通道采样（enable→disable 周期）
 */
void tcrt5000_sample(void);

/**
 * @brief 获取左侧传感器ADC电压值 (mV)
 */
uint32_t tcrt5000_get_left_adc(void);

/**
 * @brief 获取中间传感器ADC电压值 (mV)
 */
uint32_t tcrt5000_get_middle_adc(void);

/**
 * @brief 获取右侧传感器ADC电压值 (mV)
 */
uint32_t tcrt5000_get_right_adc(void);

#endif // __BSP_TCRT5000_H__
