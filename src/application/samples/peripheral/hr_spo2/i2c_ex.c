#include <stdbool.h>
#include "soc_osal.h"
#include "securec.h"
#include "common_def.h"
#include "tcxo.h"
#include "i2c.h"
#include "gpio.h"
#include "pinctrl.h"

#define I2C_SDA 13
#define I2C_SCL 14

// 设置GPIO15输出高电平
#define SDA_IO_OUT_HIGH { uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_HIGH); }
// 设置GPIO15输出低电平
#define SDA_IO_OUT_LOW { uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_LOW); }

// 设置GPIO16输出高电平
#define SCL_IO_OUT_HIGH { uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_HIGH); }
// 设置GPIO16输出低电平
#define SCL_IO_OUT_LOW { uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_LOW); }

// 获取i2c sda value
static uint8_t GetI2cSdaVal(void)
{
    gpio_direction_t val = 0;
    uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(I2C_SDA, PIN_PULL_TYPE_DISABLE);
    val = uapi_gpio_get_val(I2C_SDA);
    return val;
}

// i2c停止工作
static void I2cStop(void)
{
    SCL_IO_OUT_LOW;
    SDA_IO_OUT_LOW;
    uapi_tcxo_delay_us(1);
    SCL_IO_OUT_HIGH;
    SDA_IO_OUT_HIGH;
    uapi_tcxo_delay_us(1);
}

// i2c重启
void I2cReset(void)
{
    // 配置GPIO 15为普通IO
    uapi_pin_set_mode(I2C_SDA, PIN_MODE_0);
    uint8_t val = GetI2cSdaVal();
    if(GPIO_LEVEL_HIGH == val)
    {
        uapi_pin_set_mode(I2C_SDA,PIN_MODE_2);
        uapi_pin_set_mode(I2C_SCL,PIN_MODE_2);
        printf("i2c reset failed:\r\n");
        return;
    }

    // 配置GPIO 16为普通IO
    uapi_pin_set_mode(I2C_SCL, PIN_MODE_0);

    for(int i = 0; i < 9; i++)
    {
        SCL_IO_OUT_LOW;
        uapi_tcxo_delay_us(1);

        val = GetI2cSdaVal();
        if(GPIO_LEVEL_LOW == val)
        {
            SCL_IO_OUT_HIGH;
            uapi_tcxo_delay_us(1);
        }
        else
        {
            I2cStop();
        }

    }

    val = GetI2cSdaVal();
    if(GPIO_LEVEL_HIGH == val) { I2cStop(); }

    uapi_pin_set_mode(I2C_SDA,PIN_MODE_2);
    uapi_pin_set_mode(I2C_SCL,PIN_MODE_2);
    printf("i2c start succ\n");
}
