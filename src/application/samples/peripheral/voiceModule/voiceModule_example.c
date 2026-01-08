/**
 ****************************************************************************************************
 * @file        uartDemo_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS 串口通信实验
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:Hi3863
 * 在线视频:
 * 公司网址:
 * 购买地址:
 *
 ****************************************************************************************************
 * 实验现象：使用串口实现数据收发，串口助手发送内容原封不动返回给串口助手。
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "uart.h"
#include "osal_debug.h"
#include "soc_osal.h"
#include "app_init.h"
#include "string.h"
#include "cmsis_os2.h"
#include "watchdog.h"

#define UART_BAUDRATE 115200
#define UART_DATA_BITS 8
#define UART_STOP_BITS 1
#define UART_PARITY_BIT 0
#define UART_TRANSFER_SIZE 18
#define CONFIG_UART1_TXD_PIN 15
#define CONFIG_UART1_RXD_PIN 16
#define CONFIG_UART1_PIN_MODE 1
#define CONFIG_UART1_BUS_ID 1
#define CONFIG_UART_INT_WAIT_MS 5

#define UART_TASK_STACK_SIZE 0x1000
#define UART_TASK_DURATION_MS 1000
#define UART_TASK_PRIO 17

#define DELAY_TIME_MS 1
#define UART_RECV_SIZE 10
static uint8_t uart_recv[UART_RECV_SIZE] = {0};

static uint8_t uart_rx_flag = 0;


static uart_buffer_config_t g_app_uart_buffer_config = {.rx_buffer = uart_recv,
                                                        .rx_buffer_size = UART_TRANSFER_SIZE};

static void app_uart_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_UART1_TXD_PIN, CONFIG_UART1_PIN_MODE);
    uapi_pin_set_mode(CONFIG_UART1_RXD_PIN, CONFIG_UART1_PIN_MODE);
}

static void app_uart_init_config(void)
{
    uart_attr_t attr = {.baud_rate = UART_BAUDRATE,
                        .data_bits = UART_DATA_BIT_8,
                        .stop_bits = UART_STOP_BIT_1,
                        .parity = UART_PARITY_NONE};

    uart_pin_config_t pin_config = {.tx_pin = S_MGPIO0, .rx_pin = S_MGPIO1, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE};
    uapi_uart_deinit(CONFIG_UART1_BUS_ID); // 重点，UART初始化之前需要去初始化，否则会报0x80001044
    int res = uapi_uart_init(CONFIG_UART1_BUS_ID, &pin_config, &attr, NULL, &g_app_uart_buffer_config);
    if (res != 0) {
        printf("uart init failed res = %02x\r\n", res);
    }
}

void uart_read_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    if (buffer == NULL || length == 0) {
        return;
    }
    if (memcpy_s(uart_recv, length, buffer, length) != EOK) {
        return;
    }
    uart_rx_flag = 1;
}

void *uart_task(const char *arg)
{
    unused(arg);

    app_uart_init_pin();
    app_uart_init_config();
    printf("init success\r\n");
    // 注册串口回调函数
    if (uapi_uart_register_rx_callback(1, UART_RX_CONDITION_MASK_IDLE, 1, uart_read_handler) == ERRCODE_SUCC)
    {
        printf("rx callback failed:\r\n");
    }
    
    while (1)
    {
        while (!uart_rx_flag)
        {
            osDelay(DELAY_TIME_MS);
            (void)uapi_watchdog_kick();
        }

        uart_rx_flag = 0;
        printf("uart int rx = [%x]\n", uart_recv[0]);
        memset(uart_recv, 0, UART_RECV_SIZE);
        // uapi_uart_write(UART_BUS_1, (uint8_t *)"jack", UART_RECV_SIZE, 0);
        // osDelay(500);
        // if (uapi_uart_read(UART_BUS_1, uart_recv, UART_RECV_SIZE, 0))
        // {
        //     printf("uart poll rx = %s\r\n",uart_recv);
        //     //uapi_uart_write(UART_BUS_1, uart_recv, UART_RECV_SIZE, 0);
        // }
        // (void)uapi_watchdog_kick();
       
    }

    return NULL;
}

static void uart_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)uart_task, 0, "UartTask", UART_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, UART_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

/* Run the uart_entry. */
app_run(uart_entry);