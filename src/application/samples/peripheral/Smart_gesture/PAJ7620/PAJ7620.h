#ifndef _PAJ7620_H
#define _PAJ7620_H

#include "PAJ_I2c.h"

// DEVICE ID
#define PAJ7620_I2C_ADDR  0x73
#define PAJ7620_ADDR_BASE				0xEF

// PAJ7620_REGITER_BANK_SEL
#define PAJ7620_BANK0	0
#define PAJ7620_BANK1	1

#define GES_RIGHT_FLAG				1<<0
#define GES_LEFT_FLAG				1<<1
#define GES_UP_FLAG					1<<2
#define GES_DOWN_FLAG				1<<3
#define GES_FORWARD_FLAG			1<<4
#define GES_BACKWARD_FLAG			1<<5
#define GES_CLOCKWISE_FLAG			1<<6
#define GES_COUNT_CLOCKWISE_FLAG	1<<7
#define GES_WAVE_FLAG				1<<0


bool PAJ7620_Init(void);

#endif