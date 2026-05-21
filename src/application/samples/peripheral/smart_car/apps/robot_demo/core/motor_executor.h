#ifndef MOTOR_EXECUTOR_H
#define MOTOR_EXECUTOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int8_t left;
    int8_t right;
} MotorCmdMsg;

void motor_executor_init(void);
bool motor_executor_push_cmd(int8_t left, int8_t right);

#endif
