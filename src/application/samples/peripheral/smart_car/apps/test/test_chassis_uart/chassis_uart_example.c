//
// @file chassis_uart_example.c
// @brief 新小车底盘串口驱动测试程序
// @details 测试 UART1 (GPIO_15/16, 2400波特率) 5字节数据包收发(AA speed servo1 servo2 BB)，每25ms周期循环发送
//

#include <stdio.h>
#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "../../../drivers/chassis_uart/bsp_chassis_uart.h"

#define TASK_STACK_SIZE 0x1000
#define TASK_PRIORITY   24
#define SEND_PERIOD_MS  25 // 每 25ms 一个发送周期

//
// 串口中断数据接收回调 (ISR 上下文触发)
//
static void on_chassis_packet_received(const chassis_packet_t *pkt)
{
    if (pkt == NULL) {
        return;
    }
    uint8_t m  = (uint8_t)pkt->motor_speed;
    uint8_t s1 = (uint8_t)pkt->servo1_angle;
    uint8_t s2 = (uint8_t)pkt->servo2_angle;

    printf("[Chassis RX ISR] Motor=%d, Servo1=%d, Servo2=%d | HEX: 0xAA 0x%02X 0x%02X 0x%02X 0xBB\r\n",
           pkt->motor_speed, pkt->servo1_angle, pkt->servo2_angle,
           m, s1, s2);
}

//
// 底盘串口 25ms 周期循环发送与接收测试任务
//
static void *chassis_uart_test_task(void *arg)
{
    unused(arg);

    printf("Starting Chassis UART Test (2400 Baud, GPIO15 TX / GPIO16 RX, 25ms Period)...\r\n");

    if (bsp_chassis_uart_init(on_chassis_packet_received) != 0) {
        printf("Error: Chassis UART init failed!\r\n");
        return NULL;
    }

    uint32_t tick_count = 0;
    int state = 0; // 0: 前进50, 1: 后退50, 2: 停止

    while (1) {
        int8_t motor = 0;
        int8_t servo1 = 0;
        int8_t servo2 = 0;

        // 每 3 秒 (120 * 25ms) 切换测试动作
        state = (tick_count / 120) % 3;

        switch (state) {
            case 0:
                // 对应控制示例：前进50速度，舵机1向左50，舵机2向右-50 -> AA 32 32 CD BB
                motor  = 50;
                servo1 = 50;
                servo2 = -50;
                break;
            case 1:
                // 后退50速度，舵机1向右-50，舵机2向左50 -> AA CD CD 32 BB
                motor  = -50;
                servo1 = -50;
                servo2 = 50;
                break;
            case 2:
            default:
                // 停止，舵机居中 -> AA 00 00 00 BB
                motor  = 0;
                servo1 = 0;
                servo2 = 0;
                break;
        }

        // 发送 5 字节串口控制帧 (AA [motor] [servo1] [servo2] BB)
        bsp_chassis_uart_send(motor, servo1, servo2);

        // 尝试从消息队列非阻塞读取最新接收到的数据帧
        chassis_packet_t rx_pkt;
        if (bsp_chassis_uart_recv(&rx_pkt, 0)) {
            printf("[Chassis Queue Recv] Motor=%d, Servo1=%d, Servo2=%d\r\n",
                   rx_pkt.motor_speed, rx_pkt.servo1_angle, rx_pkt.servo2_angle);
        }

        tick_count++;
        osal_msleep(SEND_PERIOD_MS); // 维持 25ms 发送周期
    }

    return NULL;
}

//
// 应用入口注册
//
static void chassis_uart_test_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)chassis_uart_test_task,
                                      NULL,
                                      "chassis_uart_task",
                                      TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, TASK_PRIORITY);
    } else {
        printf("Error: Failed to create chassis_uart_task\r\n");
    }
    osal_kthread_unlock();
}

app_run(chassis_uart_test_entry);
