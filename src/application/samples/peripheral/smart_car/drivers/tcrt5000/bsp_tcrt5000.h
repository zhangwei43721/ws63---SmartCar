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
#define TCRT5000_BLACK 0 // 检测到黑线
#define TCRT5000_WHITE 1 // 检测到白色/无黑线

// ADC阈值定义（mV）— 应用层根据阈值判定黑白线
#define TCRT5000_LEFT_THRESHOLD  1750
#define TCRT5000_MIDDLE_THRESHOLD 1150 
#define TCRT5000_RIGHT_THRESHOLD 1600 

/**
 * @brief 原子读取三路 ADC 当前快照（mV）
 */
void tcrt5000_snapshot(uint32_t *l, uint32_t *m, uint32_t *r);

// 初始化TCRT5000 ADC模式
void tcrt5000_adc_init(void);

// 触发一次三通道采样（enable→disable 周期）
void tcrt5000_sample(void);

#endif // __BSP_TCRT5000_H__
