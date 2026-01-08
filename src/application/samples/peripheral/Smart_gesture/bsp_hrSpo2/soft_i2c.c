#include <stdbool.h>
#include "soc_osal.h"
#include "securec.h"
#include "common_def.h"
#include "tcxo.h"
#include "i2c.h"
#include "gpio.h"
#include "pinctrl.h"
#include "gpio.h"
#include "hal_gpio.h"

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

// 读取sda
uint8_t ReadSDA(void)
{
    gpio_direction_t val = 0;
    uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(I2C_SDA, PIN_PULL_TYPE_DISABLE);

    val = uapi_gpio_get_val(I2C_SDA);

    return val;
}

// 初始化I2C
void I2cInit(void)
{
    uapi_pin_set_mode(I2C_SDA,PIN_MODE_0);
    uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(I2C_SDA, PIN_PULL_TYPE_DISABLE);

    uapi_pin_set_mode(I2C_SCL,PIN_MODE_0);
    uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(I2C_SCL, PIN_PULL_TYPE_DISABLE);
    uapi_tcxo_delay_us(20);
}

// 启动I2C
void I2cStart(void)
{
    SDA_IO_OUT_HIGH;
    SCL_IO_OUT_HIGH;
    uapi_tcxo_delay_us(4);
    SDA_IO_OUT_LOW;
    uapi_tcxo_delay_us(4);
    SCL_IO_OUT_LOW;
}

// 停止I2C
void I2cStop(void)
{
    SCL_IO_OUT_LOW;
    SDA_IO_OUT_LOW;
    uapi_tcxo_delay_us(5);
    SCL_IO_OUT_HIGH;
    SDA_IO_OUT_HIGH;
    uapi_tcxo_delay_us(5);
}

// 应答I2C
// 返回值 ： 1， 接收应答失败
//           0, 接收应答成功 
uint8_t I2cWaitAck(void)
{
    uint8_t ucErrTime = 0;
    // SDA设置为输入模式
    uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT);   
    SDA_IO_OUT_HIGH;
    uapi_tcxo_delay_us(1);
    SCL_IO_OUT_HIGH;
    uapi_tcxo_delay_us(1);
    while (ReadSDA())
    {
        ucErrTime++;
        if(ucErrTime > 250)
        {
            I2cStop();
            return 1;
        }
    }
    SCL_IO_OUT_LOW; //时钟输出0
    return 0;
}

// i2c发送应答
void I2cSendAck(void)
{
    SCL_IO_OUT_LOW;
	uapi_tcxo_delay_us(1);
	SDA_IO_OUT_LOW;
	uapi_tcxo_delay_us(1);
	SCL_IO_OUT_HIGH;
	uapi_tcxo_delay_us(1);
	SCL_IO_OUT_LOW;
	uapi_tcxo_delay_us(1);
}

// i2c无应答
void I2cSendNoAck(void)
{
    SCL_IO_OUT_LOW;
	SDA_IO_OUT_HIGH;
	uapi_tcxo_delay_us(2);
	SCL_IO_OUT_HIGH;
	uapi_tcxo_delay_us(2);
	SCL_IO_OUT_LOW;
}

// i2c写字节
void I2cWriteByte(uint8_t byte)
{
    uint8_t i = 8;
    while (i--)
    {
        SCL_IO_OUT_LOW;
        uapi_tcxo_delay_us(1);

        if(byte&0x80) { SDA_IO_OUT_HIGH; }
        else { SDA_IO_OUT_LOW; }

        byte <<= 1;
        uapi_tcxo_delay_us(1);
        SCL_IO_OUT_HIGH;
        uapi_tcxo_delay_us(1);
    }//
    
    SCL_IO_OUT_LOW;
}

// i2c读字节
uint8_t I2cReadByte(unsigned char ack)
{
    unsigned char i,receive=0;
    // SDA设置为输入模式
    uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT);
    for(i = 0; i < 8; i++)
    {
        SCL_IO_OUT_LOW;
        uapi_tcxo_delay_us(2);
        SCL_IO_OUT_HIGH;
        receive <<= 1;
        if(ReadSDA()) receive++;
        uapi_tcxo_delay_us(1);
    }

    if(!ack)
        I2cSendNoAck(); // 发送NoAck
    else
        I2cSendAck(); //  发送Ack
    return receive;
}