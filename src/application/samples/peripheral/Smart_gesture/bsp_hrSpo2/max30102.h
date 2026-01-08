#ifndef __MAX30102_H_
#define __MAX30102_H_

#include "pinctrl.h"
#include "i2c.h"
#include "algorithm.h"
#include "soc_osal.h"
#include "app_init.h"
#include "tcxo.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "cmsis_os2.h"
#include "watchdog.h"

typedef enum { //gpio的电平值
    GPIO_VALUE0 = 0,
    GPIO_VALUE1
}GpioValue;

// 管脚定义
#define MAX30102_GPIO_FUN    uapi_gpio_set_val(DHT11_PIN, a)

void    Max30102Init(void);
void    Max30102Reset(void);
uint8_t Max30102GetHeartRate(void);
uint8_t Max30102GetSpO2(void);
bool    Max30102GetFingerStatus(void);
int     StartMax30102Task(void);
void    StopMax30102Task(void);

int     Max30102WriteReg(uint8_t addr, uint8_t data);
int     Max30102ReadReg(uint8_t addr, uint8_t *pData, uint16_t len);


// 设置为输入模式
void    Max30102SetInput(void);

void*    Max30102Task(void *arg);
void    FlagClear(void);



#endif // !__MAX30102_H
