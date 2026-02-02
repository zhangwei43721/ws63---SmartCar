/**
 * @file bsp_uart.h
 * @brief UART串口驱动 - 用于智能小车串口命令接收
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 数据接收回调函数类型
 * @param data 接收到的数据指针
 * @param length 数据长度
 */
typedef void (*uart_data_callback_t)(const uint8_t *data, uint16_t length);

/**
 * @brief 初始化UART串口接收
 * @param callback 数据接收回调函数
 * @return 0成功，非0失败
 * @note 波特率9600，引脚TX=8, RX=7，使用中断模式
 */
int bsp_uart_init(uart_data_callback_t callback);

/**
 * @brief UART发送数据
 * @param data 数据指针
 * @param length 数据长度
 * @return 实际发送长度，负数表示失败
 */
int bsp_uart_send(const uint8_t *data, uint16_t length);

#endif /* BSP_UART_H */
