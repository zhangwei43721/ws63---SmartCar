#ifndef _PAJ_I2C_H
#define _PAJ_I2C_H

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <gpio.h>
#include "tcxo.h"

void I2C_Config(void);
void SDA_SetGpioMode(bool mode);
void IIC_Start(void);
void IIC_Stop(void);
void IIC_SendByte(uint8_t data);
uint8_t IIC_RecvByte(void);
int8_t IIC_RecvACK(void);
void IIC_SendACK(uint8_t ack);
void IIC_WriteReg(uint8_t i2c_addr, uint8_t reg_addr, uint8_t reg_data);
void IIC_ReadData(uint8_t i2c_addr, uint8_t reg_addr, uint8_t * pdata);
int32_t IIC_ReadDataBlock(uint8_t i2c_addr, uint8_t reg_data, uint8_t *pdata, uint32_t count);

#endif