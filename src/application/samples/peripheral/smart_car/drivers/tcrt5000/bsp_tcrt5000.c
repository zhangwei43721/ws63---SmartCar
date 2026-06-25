/**
 ****************************************************************************************************
 * @file        bsp_tcrt5000.c
 * @brief       TCRT5000 红外循迹
 ****************************************************************************************************
 */

#include "bsp_tcrt5000.h"
#include <stdio.h>
#include "adc.h"
#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

// 对应索引：0 - 左，1 - 中，2 - 右
static const uint8_t TCRT_GPIOS[3] = {12, 10, 9};
static const uint8_t TCRT_ADC_CHANNELS[3] = {5, 3, 2};

// 三路 ADC 采样共享数据
static volatile uint32_t s_adc_data[3] = {0};

/* ADC 采样完成回调 */
static void tcrt5000_adc_callback(uint8_t channel, uint32_t *buffer, uint32_t length, bool *next)
{
    *next = false; // 显式置 false
    if (length > 0 && buffer != NULL) {
        for (int i = 0; i < 3; i++) {
            if (channel == TCRT_ADC_CHANNELS[i]) {
                s_adc_data[i] = buffer[0];
                break;
            }
        }
    }
}

// 初始化 TCRT5000 
void tcrt5000_adc_init(void)
{
    uapi_adc_init(ADC_CLOCK_500KHZ);
    uapi_adc_power_en(AFE_SCAN_MODE_MAX_NUM, true);

    // 循环配置 3 路引脚
    for (int i = 0; i < 3; i++) {
        uapi_pin_set_mode(TCRT_GPIOS[i], PIN_MODE_0);
        uapi_pin_set_pull(TCRT_GPIOS[i], PIN_PULL_TYPE_DISABLE);
    }
}

// 触发采样
void tcrt5000_sample(void)
{
    adc_scan_config_t cfg = {.type = 0, .freq = 1};

    // 循环顺序使能与关闭各通道，触发 WS63 的电压转换
    for (int i = 0; i < 3; i++) {
        (void)uapi_adc_auto_scan_ch_enable(TCRT_ADC_CHANNELS[i], cfg, tcrt5000_adc_callback);
        (void)uapi_adc_auto_scan_ch_disable(TCRT_ADC_CHANNELS[i]);
    }
}

/* 一次性读取三路 ADC 值*/
void tcrt5000_snapshot(uint32_t *l, uint32_t *m, uint32_t *r)
{
    // uint32_t irq = osal_irq_lock();
    if (l)
        *l = s_adc_data[0];
    if (m)
        *m = s_adc_data[1];
    if (r)
        *r = s_adc_data[2];
    // osal_irq_restore(irq);
}
