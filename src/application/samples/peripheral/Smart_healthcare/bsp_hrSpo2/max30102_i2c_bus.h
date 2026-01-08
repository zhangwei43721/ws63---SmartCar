#ifndef _MAX30102_I2C_bus
#define _MAX30102_I2C_bus

#include <stdint.h>

int max30102_Bus_Write(uint8_t addr, uint8_t data);
uint8_t max30102_Bus_Read(uint8_t Register_Address);
void max30102_i2c_init(void);
void max30102_IIC_Send_Byte(uint8_t txd);
void max30102_FIFO_ReadBytes(uint8_t Register_Address, uint8_t* Data);



#endif