/**
 ****************************************************************************************************
 * @file        soil_demo.c
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
#include "pinctrl.h"
#include "soc_osal.h"
#include "adc.h"
#include "adc_porting.h"
#include "osal_debug.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "gpio.h"

#define SOIL_TASK_STACK_SIZE               0x1000
#define SOIL_TASK_PRIO                     (osPriority_t)(17)
#define SOIL_AUTO_SAMPLE_TEST_TIMES        1000

#define TS_GPIO_PIN 9
#define ASC_CHANNEL ADC_CHANNEL_2

#define TS_READ_TIMES	5  //土壤湿度ADC循环读取次数

uint32_t g_buffer;

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

static void *soil_task(const char *arg)
{
    UNUSED(arg);
    osal_printk("start soil sample test");
    uapi_adc_init(ADC_CLOCK_500KHZ);
    uapi_adc_power_en(AFE_SCAN_MODE_MAX_NUM, true);
    adc_scan_config_t config = {
        .type = 0,
        .freq = 1,
    };
    
    while (1)
    {
        uapi_adc_auto_scan_ch_enable(ADC_CHANNEL_2, config, test_soil_callback);
        uapi_adc_auto_scan_ch_disable(ADC_CHANNEL_2);
        uint16_t value = TS_GetData();
        printf("soil : %d\r\n",value);
        osal_msleep(1000);
    }
    
    return NULL;
}

static void adc_entry(void)
{
    osThreadAttr_t attr;

    attr.name = "ADCTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = SOIL_TASK_STACK_SIZE;
    attr.priority = SOIL_TASK_PRIO;

    if (osThreadNew((osThreadFunc_t)soil_task, NULL, &attr) == NULL) {
        /* Create task fail. */
    }
}

/* Run the adc_entry. */
app_run(adc_entry);