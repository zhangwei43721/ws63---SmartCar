//
// @file bsp_chassis_uart.c
// @brief 新小车底盘 UART 串口通信驱动 (GPIO_15/GPIO_16 UART1, 2400波特率)
//

#include "bsp_chassis_uart.h"
#include <stdio.h>
#include <string.h>
#include "pinctrl.h"
#include "uart.h"
#include "soc_osal.h"
#include "common_def.h"

#define CHASSIS_UART_BUS_ID     1 // UART1 总线
#define CHASSIS_UART_BAUDRATE   2400 // 2400 波特率
#define CHASSIS_UART_TX_PIN     15 // GPIO_15
#define CHASSIS_UART_RX_PIN     16 // GPIO_16
#define CHASSIS_UART_PIN_MODE   1  // UART 模式

#define RX_QUEUE_MAX_PKTS       16
#define RX_BUFFER_SIZE          64

static uint8_t g_uart_rx_raw_buf[RX_BUFFER_SIZE] = {0};
static chassis_uart_rx_callback_t g_rx_callback = NULL;
static unsigned long g_rx_queue = 0;

static uint8_t g_parse_buf[5] = {0};
static uint8_t g_parse_idx = 0;

//
// 串口中断接收处理（滑动窗口解析 5 字节数据帧: AA [motor] [servo1] [servo2] BB）
//
static void chassis_uart_rx_isr(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    if (buffer == NULL || length == 0) {
        return;
    }

    const uint8_t *pdata = (const uint8_t *)buffer;
    for (uint16_t i = 0; i < length; i++) {
        uint8_t byte = pdata[i];
        if (g_parse_idx < 5) {
            g_parse_buf[g_parse_idx++] = byte;
        } else {
            g_parse_buf[0] = g_parse_buf[1];
            g_parse_buf[1] = g_parse_buf[2];
            g_parse_buf[2] = g_parse_buf[3];
            g_parse_buf[3] = g_parse_buf[4];
            g_parse_buf[4] = byte;
        }

        if (g_parse_idx == 5) {
            if (g_parse_buf[0] == 0xAA && g_parse_buf[4] == 0xBB) {
                chassis_packet_t pkt;
                pkt.motor_speed  = (int8_t)g_parse_buf[1]; // 第二组：电机
                pkt.servo1_angle = (int8_t)g_parse_buf[2]; // 第三组：舵机1
                pkt.servo2_angle = (int8_t)g_parse_buf[3]; // 第四组：舵机2

                if (g_rx_queue != 0) {
                    osal_msg_queue_write_copy(g_rx_queue, &pkt, sizeof(chassis_packet_t), 0);
                }
                if (g_rx_callback != NULL) {
                    g_rx_callback(&pkt);
                }
                g_parse_idx = 0;
            }
        }
    }
}

int bsp_chassis_uart_init(chassis_uart_rx_callback_t rx_cb)
{
    g_rx_callback = rx_cb;
    g_parse_idx = 0;

    if (g_rx_queue == 0) {
        osal_msg_queue_create("chassis_rx_q", RX_QUEUE_MAX_PKTS, &g_rx_queue, 0, sizeof(chassis_packet_t));
    }

    // 1. 设置引脚复用: GPIO_15 (TX), GPIO_16 (RX)
    uapi_pin_set_mode(CHASSIS_UART_TX_PIN, CHASSIS_UART_PIN_MODE);
    uapi_pin_set_mode(CHASSIS_UART_RX_PIN, CHASSIS_UART_PIN_MODE);

    // 2. 配置 UART 属性
    uart_attr_t attr = {
        .baud_rate = CHASSIS_UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity    = UART_PARITY_NONE
    };

    uart_pin_config_t pin_config = {
        .tx_pin = 15, .rx_pin = 16, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE
    };

    uart_buffer_config_t buffer_config = {
        .rx_buffer = g_uart_rx_raw_buf,
        .rx_buffer_size = RX_BUFFER_SIZE
    };

    uapi_uart_deinit(CHASSIS_UART_BUS_ID);
    int ret = uapi_uart_init(CHASSIS_UART_BUS_ID, &pin_config, &attr, NULL, &buffer_config);
    if (ret != 0) {
        printf("Chassis UART init failed: %d\r\n", ret);
        return -1;
    }

    uapi_uart_register_rx_callback(CHASSIS_UART_BUS_ID, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1, chassis_uart_rx_isr);
    printf("Chassis UART1 initialized (GPIO15/16, 2400 Baud)\r\n");
    return 0;
}

int bsp_chassis_uart_send(int8_t motor_speed, int8_t servo1_angle, int8_t servo2_angle)
{
    uint8_t tx_buf[5] = { 0xAA, (uint8_t)motor_speed, (uint8_t)servo1_angle, (uint8_t)servo2_angle, 0xBB };
    int32_t bytes_sent = uapi_uart_write(CHASSIS_UART_BUS_ID, tx_buf, 5, 0);
    return (bytes_sent == 5) ? 0 : -1;
}

int bsp_chassis_uart_send_pkt(const chassis_packet_t *pkt)
{
    if (pkt == NULL) {
        return -1;
    }
    return bsp_chassis_uart_send(pkt->motor_speed, pkt->servo1_angle, pkt->servo2_angle);
}

bool bsp_chassis_uart_recv(chassis_packet_t *pkt, uint32_t timeout_ms)
{
    if (pkt == NULL || g_rx_queue == 0) {
        return false;
    }
    uint32_t msg_size = sizeof(chassis_packet_t);
    int ret = osal_msg_queue_read_copy(g_rx_queue, pkt, &msg_size, timeout_ms);
    return (ret == OSAL_SUCCESS);
}
