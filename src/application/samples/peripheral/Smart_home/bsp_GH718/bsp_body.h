#ifndef BSP_BODY_H
#define BSP_BODY_H

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "app_init.h"

bool getBodyState(void);
void setBodayState(bool state);
void body_init(void);

#endif