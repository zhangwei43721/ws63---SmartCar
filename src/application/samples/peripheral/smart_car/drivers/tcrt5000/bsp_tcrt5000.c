/**
 ****************************************************************************************************
 * @file        bsp_tcrt5000.c
 * @brief       TCRT5000 红外循迹（ADC 自动扫描 + 共享数据加保护）
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 */

#include "bsp_tcrt5000.h"

#include <stdbool.h>
#include <stdio.h>

#include "adc.h"
#include "pinctrl.h"
#include "soc_osal.h"

// 三路 ADC 采样值。ADC 回调写入、消费者读取；32-bit 单值原子，
// 但读三个时需要"快照"接口保证组合一致性。
static volatile uint32_t s_adc_data[3] = {0};

void tcrt5000_adc_callback(uint8_t channel, uint32_t *buffer, uint32_t length, bool *next)
{
    if (length > 0 && buffer != NULL) {
        if (channel == TCRT5000_LEFT_ADC_CHANNEL) {
            s_adc_data[0] = buffer[0];
        } else if (channel == TCRT5000_MIDDLE_ADC_CHANNEL) {
            s_adc_data[1] = buffer[0];
        } else if (channel == TCRT5000_RIGHT_ADC_CHANNEL) {
            s_adc_data[2] = buffer[0];
        }
    }
    // WS63 HAL 不读取 *next，单次/持续语义由上层 enable/disable 决定
    *next = false;
}

void tcrt5000_init(void)
{
    tcrt5000_adc_init();
}

void tcrt5000_adc_init(void)
{
    uapi_adc_init(ADC_CLOCK_500KHZ);
    uapi_adc_power_en(AFE_SCAN_MODE_MAX_NUM, true);

    uapi_pin_set_mode(TCRT5000_LEFT_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_LEFT_GPIO, PIN_PULL_TYPE_DISABLE);
    uapi_pin_set_mode(TCRT5000_MIDDLE_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_MIDDLE_GPIO, PIN_PULL_TYPE_DISABLE);
    uapi_pin_set_mode(TCRT5000_RIGHT_GPIO, PIN_MODE_0);
    uapi_pin_set_pull(TCRT5000_RIGHT_GPIO, PIN_PULL_TYPE_DISABLE);
}

// WS63 ADC v154 实现：只有 ch_disable 才会触发回调把 FIFO 转成电压
// 必须 enable→disable 一来一回，由 sensor_task 周期性触发采样
void tcrt5000_sample(void)
{
    adc_scan_config_t cfg = {.type = 0, .freq = 1};
    (void)uapi_adc_auto_scan_ch_enable(TCRT5000_LEFT_ADC_CHANNEL, cfg, tcrt5000_adc_callback);
    (void)uapi_adc_auto_scan_ch_disable(TCRT5000_LEFT_ADC_CHANNEL);

    (void)uapi_adc_auto_scan_ch_enable(TCRT5000_MIDDLE_ADC_CHANNEL, cfg, tcrt5000_adc_callback);
    (void)uapi_adc_auto_scan_ch_disable(TCRT5000_MIDDLE_ADC_CHANNEL);

    (void)uapi_adc_auto_scan_ch_enable(TCRT5000_RIGHT_ADC_CHANNEL, cfg, tcrt5000_adc_callback);
    (void)uapi_adc_auto_scan_ch_disable(TCRT5000_RIGHT_ADC_CHANNEL);
}

void tcrt5000_snapshot(uint32_t *l, uint32_t *m, uint32_t *r)
{
    uint32_t irq = osal_irq_lock();
    uint32_t a = s_adc_data[0];
    uint32_t b = s_adc_data[1];
    uint32_t c = s_adc_data[2];
    osal_irq_restore(irq);
    if (l) *l = a;
    if (m) *m = b;
    if (r) *r = c;
}

unsigned int tcrt5000_get_left(void)
{
    return (s_adc_data[0] >= TCRT5000_LEFT_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
}

unsigned int tcrt5000_get_middle(void)
{
    return (s_adc_data[1] >= TCRT5000_MIDDLE_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
}

unsigned int tcrt5000_get_right(void)
{
    return (s_adc_data[2] >= TCRT5000_RIGHT_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
}

uint32_t tcrt5000_get_left_adc(void)   { return s_adc_data[0]; }
uint32_t tcrt5000_get_middle_adc(void) { return s_adc_data[1]; }
uint32_t tcrt5000_get_right_adc(void)  { return s_adc_data[2]; }
