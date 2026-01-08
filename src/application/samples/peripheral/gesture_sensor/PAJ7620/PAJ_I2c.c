#include "PAJ_I2c.h"

#define I2C_SDA 13
#define I2C_SCL 14

// 设置GPIO13输出高电平
#define SDA_IO_OUT_HIGH { uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_HIGH); }
// 设置GPIO13输出低电平
#define SDA_IO_OUT_LOW { uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_LOW); }

// 设置GPIO14输出高电平
#define SCL_IO_OUT_HIGH { uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_HIGH); }
// 设置GPIO14输出低电平
#define SCL_IO_OUT_LOW { uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_LOW); }

#define SDA_INPUT uapi_gpio_get_val(I2C_SDA)

// 设置SDA 推完输出
//#define MAX30102_IIC_SDA_OUT {uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); }
// 设置SDA 上拉输入
//#define MAX30102_IIC_SDA_IN {uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT); uapi_pin_set_pull(I2C_SDA, PIN_PULL_TYPE_UP); }

void I2C_Config(void)
{
    // SDA
    uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_HIGH);

    // SCL
    uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_HIGH);
}

/***************************************************************************************************************
*switch SDA GPIO input or output
****************************************************************************************************************/
void SDA_SetGpioMode(bool mode)
{
    if(mode == 1)
    {
        uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT);
    }
    else
    {
        uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT);
    }
}

/***************************************************************************************************************
*Simulated I2C start
****************************************************************************************************************/
void IIC_Start(void)
{
    SDA_IO_OUT_HIGH;
    SCL_IO_OUT_HIGH;
    uapi_tcxo_delay_us(30);
    SDA_IO_OUT_LOW;
    uapi_tcxo_delay_us(30);
    SCL_IO_OUT_LOW;
}

/***************************************************************************************************************
*Simulated I2C STOP
****************************************************************************************************************/
void IIC_Stop(void)
{
    SCL_IO_OUT_LOW;
    SDA_IO_OUT_LOW;
    uapi_tcxo_delay_us(30);
    SDA_IO_OUT_HIGH;
    SCL_IO_OUT_HIGH;
    uapi_tcxo_delay_us(30);
}

/***************************************************************************************************************
* send one byte
****************************************************************************************************************/
void IIC_SendByte(uint8_t data)
{
    uint8_t i = 0;
    for(i = 0; i < 8; i++)
    {
        if(data & 0x80)
        {
            SDA_IO_OUT_HIGH;
        }
        else   
        {
            SDA_IO_OUT_LOW;
        }
        SCL_IO_OUT_HIGH;
        uapi_tcxo_delay_us(30);
        SCL_IO_OUT_LOW;
        uapi_tcxo_delay_us(30);
        data <<= 1;
    }
}

/***************************************************************************************************************
* receive one byte
****************************************************************************************************************/
uint8_t IIC_RecvByte(void)
{
    uint8_t i;
    uint8_t temp = 0;
    SDA_SetGpioMode(0); // set sda input
    for (i = 0; i < 8; i++)
    {
        SCL_IO_OUT_LOW;
        temp <<= 1;
        SCL_IO_OUT_HIGH;
        uapi_tcxo_delay_us(30); // if ID is not 0xAB,try to modification value
        temp |= SDA_INPUT;
        SCL_IO_OUT_LOW;
        uapi_tcxo_delay_us(30); // if ID is not 0xAB,try to modification value
    }
    SDA_SetGpioMode(1); // set sda gpio output
    return temp;
}

/***************************************************************************************************************
* wait for a response
****************************************************************************************************************/
int8_t IIC_RecvACK(void)
{
    uint8_t wait = 0xff;
    SDA_SetGpioMode(0); // set sda input
    SCL_IO_OUT_HIGH;
    while(SDA_INPUT && wait--);
    if(wait <= 0)
    {
        IIC_Stop();
        return -1;
    }
    SDA_INPUT;
    SCL_IO_OUT_LOW;
    uapi_tcxo_delay_us(30);
    SDA_SetGpioMode(1); // set sda gpio output
    return 0;
}

/***************************************************************************************************************
*response ACK; ack==1,response; ack==0,none; 
****************************************************************************************************************/
void IIC_SendACK(uint8_t ack)
{
    SCL_IO_OUT_LOW;
    if (ack)
    {
        SDA_IO_OUT_HIGH;
    }      
    else
    {
        SDA_IO_OUT_LOW;
    }
    uapi_tcxo_delay_us(30);
    SCL_IO_OUT_HIGH;
    uapi_tcxo_delay_us(30);
    SCL_IO_OUT_LOW;
}

/***************************************************************************************************************
* I2C write data
****************************************************************************************************************/
void IIC_WriteReg(uint8_t i2c_addr, uint8_t reg_addr, uint8_t reg_data)
{
    IIC_Start();
    IIC_SendByte((i2c_addr<<1)|0);
    IIC_RecvACK(); 
    IIC_SendByte(reg_addr);
    IIC_RecvACK();
    IIC_SendByte(reg_data);
    IIC_RecvACK();
    IIC_Stop();
}

/***************************************************************************************************************
*receive one bytes to 'pdata'
****************************************************************************************************************/
void IIC_ReadData(uint8_t i2c_addr, uint8_t reg_addr, uint8_t *pdata)
{
    IIC_Start();
    IIC_SendByte((i2c_addr<<1)|0x00);
    IIC_RecvACK();
    IIC_SendByte(reg_addr);
    IIC_RecvACK();
    IIC_Start();
    IIC_SendByte((i2c_addr<<1)|0x01);
    IIC_RecvACK();
    *pdata=IIC_RecvByte();
    IIC_SendACK(1);
    IIC_Stop();
}

/***************************************************************************************************************
*Reads a block (array) of bytes from the I2C device and register
****************************************************************************************************************/
int32_t IIC_ReadDataBlock(uint8_t i2c_addr, uint8_t reg_data, uint8_t *pdata, uint32_t count)
{
    uint32_t i = 0;
    uint32_t y = 0;
    IIC_Start(); 
    IIC_SendByte((i2c_addr<<1)|0x00);	
    if(IIC_RecvACK()==-1)
    {
        IIC_Stop();		 
        return -1;
    }
    IIC_SendByte(reg_data);
    IIC_RecvACK();
    IIC_Start();
	IIC_SendByte((i2c_addr<<1)|0x01);
    IIC_RecvACK();
    for(i = 0; i <count; i++)
    {
        *pdata=IIC_RecvByte();
        if(count==(i+1))
            IIC_SendACK(1);
        else
            IIC_SendACK(0);
        pdata++;
        y++;
    }
    IIC_Stop();
    return y;
}