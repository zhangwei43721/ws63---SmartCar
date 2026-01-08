#ifndef _SOFT_I2C_H
#define _SOFT_I2C_H
#include <stdint.h>

void I2cInit(void);
void I2cStart(void);
void I2cStop(void);
uint8_t I2cWaitAck(void);
void I2cSendAck(void);
void I2cSendNoAck(void);
void I2cWriteByte(uint8_t byte);
uint8_t I2cReadByte(unsigned char ack);

#endif