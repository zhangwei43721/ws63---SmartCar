#ifndef MOTOR_EXECUTOR_H
#define MOTOR_EXECUTOR_H

#include <stdint.h>
#include <stdbool.h>

#define MOTOR_SRC_REMOTE  0x01
#define MOTOR_SRC_AUTO    0x02
#define MOTOR_SRC_VOICE   0x03

typedef struct {
    int8_t left;
    int8_t right;
    uint32_t source;
} MotorCmdMsg;

void motor_executor_init(void);
bool motor_executor_push_cmd(int8_t left, int8_t right, uint32_t source);

#endif
