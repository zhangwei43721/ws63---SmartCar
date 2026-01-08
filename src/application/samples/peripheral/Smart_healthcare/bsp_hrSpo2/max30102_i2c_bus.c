#include "soft_i2c.h"
#include "gpio.h"
#include <stdio.h>
#include "tcxo.h"
#include "max30102_i2c_bus.h"

#define I2C_WR	0		/* 写控制bit */
#define I2C_RD	1		/* 读控制bit */

#define I2C_WRITE_ADDR 0xAE
#define I2C_READ_ADDR 0xAF

#define max30102_WR_address 0xAE

#define I2C_SDA             13                 //I2C1 SDA对应引脚
#define I2C_SCL             14                 //I2C1 SCL对应引脚

// 设置GPIO15输出高电平
#define SDA_IO_OUT_HIGH { uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_HIGH); }
// 设置GPIO15输出低电平
#define SDA_IO_OUT_LOW { uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SDA, GPIO_LEVEL_LOW); }

// 设置GPIO16输出高电平
#define SCL_IO_OUT_HIGH { uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_HIGH); }
// 设置GPIO16输出低电平
#define SCL_IO_OUT_LOW { uapi_gpio_set_dir(I2C_SCL, GPIO_DIRECTION_OUTPUT); uapi_gpio_set_val(I2C_SCL, GPIO_LEVEL_LOW); }

// 设置SDA 推完输出
#define MAX30102_IIC_SDA_OUT {uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_OUTPUT); }
// 设置SDA 上拉输入
#define MAX30102_IIC_SDA_IN {uapi_gpio_set_dir(I2C_SDA, GPIO_DIRECTION_INPUT); uapi_pin_set_pull(I2C_SDA, PIN_PULL_TYPE_UP); }


//register addresses
#define REG_INTR_STATUS_1           0x00
#define REG_INTR_STATUS_2           0x01
#define REG_INTR_ENABLE_1           0x02
#define REG_INTR_ENABLE_2           0x03
#define REG_FIFO_WR_PTR             0x04
#define REG_OVF_COUNTER             0x05
#define REG_FIFO_RD_PTR             0x06
#define REG_FIFO_DATA               0x07
#define REG_FIFO_CONFIG             0x08
#define REG_MODE_CONFIG             0x09
#define REG_SPO2_CONFIG             0x0A
#define REG_LED1_PA                 0x0C
#define REG_LED2_PA                 0x0D
#define REG_PILOT_PA                0x10
#define REG_MULTI_LED_CTRL1         0x11
#define REG_MULTI_LED_CTRL2         0x12
#define REG_TEMP_INTR               0x1F
#define REG_TEMP_FRAC               0x20
#define REG_TEMP_CONFIG             0x21
#define REG_PROX_INT_THRESH         0x30
#define REG_REV_ID                  0xFE
#define REG_PART_ID                 0xFF

// 写入数据
// 返回值 : 0 写入成功  1 写入失败
int max30102_Bus_Write(uint8_t addr, uint8_t data)
{
    // 1. 发起IIC总线启动信号
    I2cStart();

    // 2. 发起控制字节， 高7bit是地址，bit0是读写控制位，0表示写，1表示读
    //I2cWriteByte(max30102_WR_address | I2C_WR);
    
    max30102_IIC_Send_Byte(max30102_WR_address | I2C_WR);
    // 3. 发送ack
    if(I2cWaitAck() != 0)
    {
        printf("send max30102_WR_address ack failed.\r\n");
        goto cmd_fail;
    }

    // 4. 发送字节地址
    max30102_IIC_Send_Byte(addr);
    if(I2cWaitAck() != 0)
    {
        printf("send addr ack faild.\r\n");
        goto cmd_fail;
    }

    // 5. 开始写入数据
    max30102_IIC_Send_Byte(data);
    if(I2cWaitAck() != 0)
    {
        printf("send data ack faild.\r\n");
        goto cmd_fail;
    }

    // 6. 发送停止信号
    I2cStop();
    //printf("write succ\r\n");
    return 1; 

cmd_fail:
    I2cStop();
    printf("write byte failed.\r\n");
    return 0;
}

uint8_t max30102_Bus_Read(uint8_t Register_Address)
{
    uint8_t data;
    // 1. 发起IIC总线启动信号
    I2cStart();

    /* 第2步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	max30102_IIC_Send_Byte(max30102_WR_address | I2C_WR);	/* 此处是写指令 */

    /* 第3步：发送ACK */
    if(I2cWaitAck() != 0)
    {
        printf("send max30102_WR_address ack failed.\r\n");
        goto cmd_fail;
    }

    /* 第4步：发送字节地址 */
    max30102_IIC_Send_Byte(Register_Address);
    if(I2cWaitAck() != 0)
    {
        printf("send max30102_WR_address ack failed.\r\n");
        goto cmd_fail;
    }

    // 第5步：重新启动i2c总线，下面开始读取数据
    I2cStart();

    /* 第6步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
    max30102_IIC_Send_Byte(max30102_WR_address | I2C_RD); // 此处是读指令

    /* 第7步：发送ACK */
    if(I2cWaitAck() != 0)
    {
        printf("send max30102 EEPROM ack failed.\r\n");
        goto cmd_fail;
    }
    // 第8步：读取数据，读一个字节
    data = I2cReadByte(0);
    I2cSendNoAck();

    // 发送I2c总线停止信号
    I2cStop();
    //printf("read byte succ.\r\n");
    return data;

cmd_fail:
    // 发送I2c总线停止信号
    I2cStop();
    printf("read byte failed.\r\n");
    return 0;
}

//IIC发送一个字节
//返回从机有无应答
//1，有应答
//0，无应答	
void max30102_IIC_Send_Byte(uint8_t txd)
{
    uint8_t t;
    MAX30102_IIC_SDA_OUT;
    // 拉低时钟开始数据传输
    SCL_IO_OUT_LOW;

    for(t = 0; t < 8; t++)
    {
        uapi_gpio_set_val(I2C_SDA, (txd&0x80)>>7);
        txd <<= 1;
        uapi_tcxo_delay_us(2); //对TEA5767这三个延时都是必须的
        SCL_IO_OUT_HIGH;
        uapi_tcxo_delay_us(2);
        SCL_IO_OUT_LOW;
        uapi_tcxo_delay_us(2);
    }

}


void max30102_FIFO_ReadBytes(uint8_t Register_Address, uint8_t* Data)
{
    max30102_Bus_Read(REG_INTR_STATUS_1);
    max30102_Bus_Read(REG_INTR_STATUS_2);

    // 第1步：发起I2C总线启动信号
    I2cStart();

    // 第2步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读
    max30102_IIC_Send_Byte(max30102_WR_address | I2C_WR); /* 此处是写指令 */

    // 第3步：发送ACK
    if(I2cWaitAck() != 0)
    {
        goto cmd_fail; /* EEPROM器件无应答 */
    }

    /* 第4步：发送字节地址， */
    max30102_IIC_Send_Byte(Register_Address);
    if(I2cWaitAck() != 0)
    {
        printf("send max30102_WR_address ack failed.\r\n");
        goto cmd_fail;
    }

    /* 第5步：重新启动I2C总线。下面开始读取数据 */
    I2cStart();

    /* 第6步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
    max30102_IIC_Send_Byte(max30102_WR_address | I2C_RD); /* 此处是读指令 */
    /* 第7步：发送ACK */
    if(I2cWaitAck() != 0)
    {
        goto cmd_fail;
    }

    /* 第8步：读取数据 */
    Data[0] = I2cReadByte(1);	
	Data[1] = I2cReadByte(1);	
	Data[2] = I2cReadByte(1);	
	Data[3] = I2cReadByte(1);
	Data[4] = I2cReadByte(1);	
	Data[5] = I2cReadByte(0);

    /* 最后1个字节读完后，CPU产生NACK信号(驱动SDA = 1) */
	/* 发送I2C总线停止信号 */
    I2cStop();
cmd_fail:
    I2cStop();
}


// 初始化max30102 i2c
void max30102_i2c_init(void)
{
    SDA_IO_OUT_HIGH;
    SCL_IO_OUT_HIGH;
}



