/**
 * @file bsp_uart.c
 * @brief UART串口驱动实现 - 用于智能小车串口命令接收
 */

#include "bsp_uart.h"

#include "pinctrl.h"
#include "securec.h"
#include "soc_osal.h"
#include "uart.h"

/* UART配置参数 */
#define UART_BAUDRATE 9600
#define UART_RX_BUFFER_SIZE 64

/* UART引脚配置 */
#define UART_TXD_PIN 8
#define UART_RXD_PIN 7
#define UART_TXD_PIN_MODE 1
#define UART_RXD_PIN_MODE 1

/* UART总线ID */
#define UART_BUS_ID 2

/* 接收缓冲区 */
static uint8_t g_uart_rx_buffer[UART_RX_BUFFER_SIZE] = {0};

/* 回调函数指针 */
static uart_data_callback_t g_data_callback = NULL;

/**
 * @brief UART接收中断回调函数
 * @param buffer 接收到的数据指针
 * @param length 数据长度
 * @param error 错误标志
 */
static void uart_rx_interrupt_handler(const void* buffer, uint16_t length,
                                      bool error) {
  unused(error);

  if (buffer == NULL || length == 0 || g_data_callback == NULL) {
    return;
  }

  g_data_callback((const uint8_t*)buffer, length);
}

/**
 * @brief 初始化UART引脚
 */
static void uart_init_pin(void) {
#if defined(CONFIG_PINCTRL_SUPPORT_IE)
  uapi_pin_set_ie(UART_RXD_PIN, PIN_IE_1);
#endif
  uapi_pin_set_mode(UART_TXD_PIN, UART_TXD_PIN_MODE);
  uapi_pin_set_mode(UART_RXD_PIN, UART_RXD_PIN_MODE);
}

/**
 * @brief 初始化UART配置
 */
static void uart_init_config(void) {
  uart_attr_t attr = {.baud_rate = UART_BAUDRATE,
                      .data_bits = UART_DATA_BIT_8,
                      .stop_bits = UART_STOP_BIT_1,
                      .parity = UART_PARITY_NONE};

  uart_pin_config_t pin_config = {.tx_pin = UART_TXD_PIN,
                                  .rx_pin = UART_RXD_PIN,
                                  .cts_pin = PIN_NONE,
                                  .rts_pin = PIN_NONE};

  uart_buffer_config_t buffer_config = {.rx_buffer = g_uart_rx_buffer,
                                        .rx_buffer_size = UART_RX_BUFFER_SIZE};

  uapi_uart_deinit(UART_BUS_ID);
  uapi_uart_init(UART_BUS_ID, &pin_config, &attr, NULL, &buffer_config);
}

/**
 * @brief 注册UART接收回调
 */
static void uart_register_rx_callback(void) {
  uapi_uart_register_rx_callback(
      UART_BUS_ID, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1,
      uart_rx_interrupt_handler);
}

int bsp_uart_init(uart_data_callback_t callback) {
  if (callback == NULL) {
    return -1;
  }

  g_data_callback = callback;

  uart_init_pin();              // 初始化引脚
  uart_init_config();           // 初始化UART配置
  uart_register_rx_callback();  // 注册接收回调

  printf("BSP_UART: UART%d initialized, baudrate=%d\r\n", UART_BUS_ID,
         UART_BAUDRATE);

  return 0;
}

int bsp_uart_send(const uint8_t* data, uint16_t length) {
  if (data == NULL || length == 0) {
    return -1;
  }

  return uapi_uart_write(UART_BUS_ID, data, length, 0);
}
