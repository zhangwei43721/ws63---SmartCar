#ifndef _BSP_DHT11_H
#define _BSP_DHT11_H

#include "pinctrl.h"
#include "soc_osal.h"
#include "app_init.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "tcxo.h"
#include "watchdog.h"

// 定义管脚
#define DHT11_PIN 7

#define DHT11_DQ_OUT(a) uapi_gpio_set_val(DHT11_PIN, a)

//函数声明
void dht11_io_out(void);
void dht11_io_in(void);
void dht11_reset(void);
uint8_t dht11_check(void);
uint8_t dht11_read_bit(void);
uint8_t dht11_read_byte(void);
uint8_t dht11_read_data(uint8_t *temp,uint8_t *humi) ;
uint8_t dht11_init(void);

#endif