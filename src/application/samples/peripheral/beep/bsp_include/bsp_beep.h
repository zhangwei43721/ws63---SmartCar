#ifndef BSP_BEEP_H
#define BSP_BEEP_H

#include "pinctrl.h"
#include "soc_osal.h"
#include "app_init.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "tcxo.h"

// 管脚定义
#define BEEP_PIN 5

#define BEEP(a) uapi_gpio_set_val(BEEP_PIN,a)

// 函数声明
void beep_init(void);
void beep_alarm(uint16_t cnt, uint16_t time);

#endif