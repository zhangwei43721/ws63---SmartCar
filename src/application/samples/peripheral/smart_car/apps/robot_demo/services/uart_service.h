/**
 * @file uart_service.h
 * @brief UART命令服务 - 协议定义
 */

#ifndef UART_SERVICE_H
#define UART_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/* 
 * 协议定义 (单字节命令)
 * 0x01-0x0F: 运动控制
 * 0x10-0x1F: 模式切换
 */
typedef enum {
    /* 运动控制 */
    UART_CMD_STOP       = 0x00, // 停止
    UART_CMD_FORWARD    = 0x01, // 前进
    UART_CMD_BACKWARD   = 0x02, // 后退
    UART_CMD_LEFT       = 0x03, // 左转
    UART_CMD_RIGHT      = 0x04, // 右转
    
    /* 模式切换 */
    UART_CMD_STANDBY    = 0x10, // 待机模式
    UART_CMD_TRACE      = 0x11, // 循迹模式
    UART_CMD_OBSTACLE   = 0x12, // 避障模式
    UART_CMD_REMOTE     = 0x13  // 遥控模式
} UartCommand;

void uart_service_init(void);
void uart_service_tick(void);
bool uart_service_is_cmd_active(void);
void uart_service_get_motor_cmd(int8_t *motor_l, int8_t *motor_r);

#endif /* UART_SERVICE_H */