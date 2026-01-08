#ifndef _I2C_BSP_H
#define _I2C_BSP_H
#include <stdint.h>

uint32_t I2cBspInit(uint32_t id, uint32_t baudrate);
uint32_t I2cBspDeinit(uint32_t id);
uint32_t I2cBspWrite(uint32_t id, uint16_t deviceAddr, uint8_t *data, uint32_t dataLen);
uint32_t I2cBspRead(uint32_t id, uint16_t deviceAddr, uint8_t *data, uint32_t dataLen);
uint32_t I2cBspSetBaudrate(uint32_t id, uint32_t baudrate);

#endif // _I2C_BSP_H