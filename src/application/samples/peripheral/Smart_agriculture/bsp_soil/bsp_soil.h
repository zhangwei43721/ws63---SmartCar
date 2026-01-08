#ifndef BSP_SOIL_H
#define BSP_SOIL_H

#include "pinctrl.h"
#include "soc_osal.h"
#include "adc.h"
#include "adc_porting.h"
#include "osal_debug.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "gpio.h"

#define SOIL_AUTO_SAMPLE_TEST_TIMES        1000

#define TS_GPIO_PIN 9
#define ASC_CHANNEL ADC_CHANNEL_2
#define TS_READ_TIMES	5  //土壤湿度ADC循环读取次数

void test_soil_callback(uint8_t ch, uint32_t *buffer, uint32_t length, bool *next);

uint16_t TS_GetData(void);

#endif