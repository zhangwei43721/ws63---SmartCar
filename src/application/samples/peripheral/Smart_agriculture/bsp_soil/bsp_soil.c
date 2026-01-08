/**
 ****************************************************************************************************
 * @file        bsp_soil.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-23
 * @brief       LiteOS soil实验
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
 * 实验现象：获取土壤湿度数据
 *
 ****************************************************************************************************
 */

#include "bsp_soil.h"

static uint32_t g_buffer;

void test_soil_callback(uint8_t ch, uint32_t *buffer, uint32_t length, bool *next) 
{ 
    UNUSED(next);
    unused(ch);
    // for (uint32_t i = 0; i < length; i++) {
    //     printf("channel: %d, voltage: %dmv\r\n", ch, buffer[i]);
    // } 
    g_buffer = buffer[length-1];
}

uint16_t TS_GetData(void)
{
    uint32_t tempData = 0;
    for(int i = 0; i < TS_READ_TIMES; i++)
    {
        tempData += g_buffer;
        osal_msleep(5);
    }
    tempData /= TS_READ_TIMES;
    return 100 - (float)tempData/40.96;
}