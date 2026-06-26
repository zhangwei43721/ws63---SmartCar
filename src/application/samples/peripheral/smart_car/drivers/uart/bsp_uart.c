/**
 * @file bsp_uart.c
 * @brief UART 串口驱动 - 用于智能小车串口命令接收
 *
 * 设计要点：
 *  - HAL RX 中断只回调上层 g_data_callback；上层 callback 必须保证 ISR-safe
 *    （当前 voice_rx_callback 只做 NO_WAIT 消息队列写入，满足要求）
 *  - g_data_callback 只在 bsp_uart_init 内一次性赋值，后续只读，无需锁
 *  - 之前尝试过加 BSP 内部派发任务，但会占用一个 LiteOS 任务槽，与 SLE/BT
 *    worker、Portal、WiFi mgr 一起把任务池吃满，导致 motor_exec/sensor_task
 *    创建失败 —— 现在 ISR 直接回调，因为业务 cb 已经不在 ISR 里做实际工作
 */

#include "bsp_uart.h"

#include <stdio.h>

#include "pinctrl.h"
#include "securec.h"
#include "soc_osal.h"
#include "uart.h"

// UART配置参数
#define UART_BAUDRATE 9600     // 串口波特率
#define UART_RX_BUFFER_SIZE 64 // 接收缓冲区大小

// UART引脚配置
#define UART_TXD_PIN 8      // 发送引脚编号
#define UART_RXD_PIN 7      // 接收引脚编号
#define UART_TXD_PIN_MODE 1 // 发送引脚复用模式
#define UART_RXD_PIN_MODE 1 // 接收引脚复用模式

// UART总线ID
#define UART_BUS_ID 2 // UART 总线 ID

// 接收缓冲区
static uint8_t g_uart_rx_buffer[UART_RX_BUFFER_SIZE] = {0};

// 回调函数指针：init 阶段一次性写入，运行期只读
static uart_data_callback_t g_data_callback = NULL;

/**
 * @brief UART HAL 接收中断回调
 * @note  运行在 ISR/软中断上下文。当前业务 callback (voice_rx_callback)
 *        只做 msg_queue_write_copy(..NO_WAIT)，是 ISR-safe 的；
 *        新接入者必须遵守同样约定，禁止在 callback 里阻塞或做重业务。
 */
static void uart_rx_interrupt_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);

    if (buffer == NULL || length == 0 || g_data_callback == NULL) {
        return;
    }

    g_data_callback((const uint8_t *)buffer, length);
}

/**
 * @brief 初始化UART引脚
 */
static void uart_init_pin(void)
{
#if defined(CONFIG_PINCTRL_SUPPORT_IE)
    uapi_pin_set_ie(UART_RXD_PIN, PIN_IE_1);
#endif
    uapi_pin_set_mode(UART_TXD_PIN, UART_TXD_PIN_MODE);
    uapi_pin_set_mode(UART_RXD_PIN, UART_RXD_PIN_MODE);
}

/**
 * @brief 初始化UART配置
 */
static void uart_init_config(void)
{
    uart_attr_t attr = {.baud_rate = UART_BAUDRATE,
                        .data_bits = UART_DATA_BIT_8,
                        .stop_bits = UART_STOP_BIT_1,
                        .parity = UART_PARITY_NONE};

    uart_pin_config_t pin_config = {
        .tx_pin = UART_TXD_PIN, .rx_pin = UART_RXD_PIN, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE};

    uart_buffer_config_t buffer_config = {.rx_buffer = g_uart_rx_buffer, .rx_buffer_size = UART_RX_BUFFER_SIZE};

    uapi_uart_deinit(UART_BUS_ID);
    uapi_uart_init(UART_BUS_ID, &pin_config, &attr, NULL, &buffer_config);
}

/**
 * @brief 注册UART接收回调
 */
static void uart_register_rx_callback(void)
{
    uapi_uart_register_rx_callback(UART_BUS_ID, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1,
                                   uart_rx_interrupt_handler);
}

// 初始化 UART：设置回调、配置引脚和波特率、注册接收中断
int bsp_uart_init(uart_data_callback_t callback)
{
    if (callback == NULL) {
        return -1;
    }

    // 回调一次性写入；后续运行期视为只读
    g_data_callback = callback;

    uart_init_pin();
    uart_init_config();
    uart_register_rx_callback();

    printf("BSP_UART: UART%d initialized, baudrate=%d\r\n", UART_BUS_ID, UART_BAUDRATE);

    return 0;
}
