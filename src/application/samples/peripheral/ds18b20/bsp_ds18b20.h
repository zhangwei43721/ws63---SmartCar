#ifndef _BSP_DS18B20_H
#define _BSP_DS18B20_H

#include "pinctrl.h"
#include "soc_osal.h"
#include "app_init.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "tcxo.h"
#include "watchdog.h"

// 定义管脚
#define DS18B20_PIN 7
#define DS18B20_GPIO_FUN HAL_PIO_FUNC_GPIO

#define DS18B20_DQ_OUT(a) uapi_gpio_set_val(DS18B20_PIN,a)

// 函数声明
void ds18b20_io_out(void);
void ds18b20_io_in(void);
void ds18b20_reset(void);
uint8_t ds18b20_check(void);
uint8_t ds18b20_read_bit(void);
uint8_t ds18b20_read_byte(void);
void ds18b20_write_byte(uint8_t dat);
void ds18b20_start(void);
uint8_t ds18b20_init(void);
uint16_t ds18b20_gettemperture(void);

#endif