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
#include "../../apps/car_demo/car_common.h" // CarDriveCmd 方向枚举

#define CHASSIS_UART_BUS_ID     1 // UART1 总线
#define CHASSIS_UART_BAUDRATE   2400 // 2400 波特率
#define CHASSIS_UART_TX_PIN     15 // GPIO_15
#define CHASSIS_UART_RX_PIN     16 // GPIO_16
#define CHASSIS_UART_PIN_MODE   1  // UART 模式

#define RX_QUEUE_MAX_PKTS       16
#define RX_BUFFER_SIZE          64

#define TX_REFRESH_PERIOD_MS    25   // 串口帧持续刷新周期（舵机需周期信号保持动作）
#define TX_STOP_RESEND_COUNT    4    // 停车帧补发次数（串口单向无 ACK，多发几次确保停下）

static uint8_t g_uart_rx_raw_buf[RX_BUFFER_SIZE] = {0};
static chassis_uart_rx_callback_t g_rx_callback = NULL;
static unsigned long g_rx_queue = 0;

static uint8_t g_parse_buf[5] = {0};
static uint8_t g_parse_idx = 0;

// ==================== TX 周期刷新状态 ====================
// 串口舵机底盘与 L9110S 的 PWM 持续输出不同，需要周期收到信号才保持动作，
// 且串口单向无 ACK，单发一帧可能丢失。故由 TX 任务按 25ms 周期刷新：
//   运动帧：每 25ms 重发一次保持动作；停车帧：额外补发多次确保可靠停下；
//   新命令到达立即唤醒发送，可打断进行中的停车补发。
static chassis_packet_t g_tx_target = {0, 0, 0}; // 目标帧（set_differential 更新）
static uint8_t g_stop_resend_remain = 0;         // 停车帧剩余补发次数
static osal_task *g_tx_task = NULL;              // TX 刷新任务句柄
static osal_event g_tx_event;                    // 0x01：新命令到达，唤醒 TX 任务
static osal_mutex g_tx_lock;                     // 保护 g_tx_target / g_stop_resend_remain
static bool g_tx_lock_inited = false;
static bool g_tx_inited = false;

// 前置声明：bsp_chassis_uart_init 在文件前部调用，实现在文件后部
static void chassis_tx_task_start(void);

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

    chassis_tx_task_start(); // 启动串口帧周期刷新任务（运动 25ms 刷新 / 停车补发）
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

// 判断帧是否含非零通道（运动帧）
static bool chassis_frame_moving(const chassis_packet_t *pkt)
{
    return pkt->motor_speed != 0 || pkt->servo1_angle != 0 || pkt->servo2_angle != 0;
}

// TX 周期刷新任务：以 osal_event_read 的 25ms 超时作为周期 tick（事件+定时，非忙等）。
// 发送条件：新命令立即发 / 运动帧持续刷新 / 停车补发未完成。
static int chassis_tx_task_entry(void *arg)
{
    (void)arg;
    printf("[Chassis] TX 刷新任务启动 (25ms 周期)\r\n");

    while (1) {
        int ret = osal_event_read(&g_tx_event, 0x01, TX_REFRESH_PERIOD_MS,
                                  OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        bool new_cmd = (ret > 0 && ((unsigned int)ret & 0x01));

        chassis_packet_t frame;
        bool do_send;
        osal_mutex_lock(&g_tx_lock);
        frame = g_tx_target;
        bool moving = chassis_frame_moving(&frame);
        do_send = new_cmd || moving || g_stop_resend_remain > 0;
        if (!moving && g_stop_resend_remain > 0) {
            g_stop_resend_remain--;
        }
        osal_mutex_unlock(&g_tx_lock);

        if (do_send) {
            (void)bsp_chassis_uart_send(frame.motor_speed, frame.servo1_angle, frame.servo2_angle);
        }
    }
    return 0;
}

// 启动 TX 周期刷新任务（仅一次）
static void chassis_tx_task_start(void)
{
    if (g_tx_inited) {
        return;
    }

    if (!g_tx_lock_inited) {
        if (osal_mutex_init(&g_tx_lock) != OSAL_SUCCESS) {
            printf("[Chassis] TX 锁初始化失败\r\n");
            return;
        }
        g_tx_lock_inited = true;
    }

    if (osal_event_init(&g_tx_event) != OSAL_SUCCESS) {
        printf("[Chassis] TX 事件初始化失败\r\n");
        return;
    }

    osal_kthread_lock();
    g_tx_task = osal_kthread_create((osal_kthread_handler)chassis_tx_task_entry, NULL, "chassis_tx", 2048);
    if (g_tx_task != NULL) {
        osal_kthread_set_priority(g_tx_task, 10);
    }
    osal_kthread_unlock();

    if (g_tx_task == NULL) {
        printf("[Chassis] TX 任务创建失败\r\n");
        return;
    }

    g_tx_inited = true;
    printf("[Chassis] TX 周期刷新就绪\r\n");
}

// 内部统一出口：把"电机速度（正=前进）+ 舵机摆幅"写为目标帧并唤醒 TX 任务。
// 参数用 int16 承接（差速转向舵机摆幅放大后可达 ±600），clamp 后再转 int8 下发。
static void chassis_apply_target(int16_t motor, int16_t servo)
{
    int16_t steering = servo;

    // 接线方向修正：底盘电机接线与协议定义相反（协议正值=前进、实际后退），
    // 前进/后退统一在此取反，set_differential 与 drive 两个入口都天然正确。
    motor = -motor;

    if (motor > 100)
        motor = 100;
    else if (motor < -100)
        motor = -100;

    if (steering > 100)
        steering = 100;
    else if (steering < -100)
        steering = -100;

    // TX 任务未就绪（初始化失败）时的退化路径：直接同步发送一帧
    if (!g_tx_inited) {
        (void)bsp_chassis_uart_send((int8_t)motor, (int8_t)steering, (int8_t)steering);
        return;
    }

    osal_mutex_lock(&g_tx_lock);
    g_tx_target.motor_speed = (int8_t)motor;
    g_tx_target.servo1_angle = (int8_t)steering;
    g_tx_target.servo2_angle = (int8_t)steering; // 两舵机同向转向（都左摆或都右摆）
    if (motor == 0 && steering == 0) {
        g_stop_resend_remain = TX_STOP_RESEND_COUNT; // 停车：补发多次确保停下
    } else {
        g_stop_resend_remain = 0; // 运动：打断进行中的停车补发
    }
    osal_mutex_unlock(&g_tx_lock);

    // 唤醒 TX 任务立即发送新帧
    (void)osal_event_write(&g_tx_event, 0x01);
}

// 差速意图 → 舵机转向底盘指令。阿克曼转向运动学只归本底盘驱动所有。
// 不直接发帧，而是更新目标帧并唤醒 TX 任务：由 TX 任务按 25ms 周期刷新、
// 停车帧补发多次（串口无 ACK 需冗余），新命令可打断进行中的停车补发。
void bsp_chassis_uart_set_differential(int8_t left, int8_t right)
{
    // 前进电机速度 = 左右轮平均；用 int16 中间量避免 int8 溢出
    int16_t motor = ((int16_t)left + right) / 2;
    // 转向舵机摆幅 = 左右轮差 * 放大增益（左右转时打满舵）
    int16_t steering = ((int16_t)right - left) * 3;

    // 舵机底盘无差速，无法原地转：差速"原地转"（前进分量 0、转向非 0）会把
    // 电机速度算成 0，导致只有舵机打方向、驱动轮不转。此时改用两侧速度幅值的
    // 平均作为前进速度，让车"边前进边转"（最小半径转弯），保证驱动轮始终转动。
    if (motor == 0 && steering != 0) {
        motor = 40; // 原地转在舵机底盘只能"低速前进+满舵"绕小圈，避免电机停或绕大圈
    }

    chassis_apply_target(motor, steering);
}

// 方向命令 → 舵机转向底盘动作（边前进边转的阿克曼转向）。
// 舵机底盘无差速电机，左/右转只能"前进电机保持前进 + 舵机打满方向"。
void bsp_chassis_uart_drive(uint8_t dir, int8_t speed)
{
    int8_t motor = 0;
    int8_t servo = 0;

    switch (dir) {
        case CAR_DRIVE_STOP:
            motor = 0;
            servo = 0;
            break;
        case CAR_DRIVE_FORWARD:
            motor = speed;
            servo = 0;
            break;
        case CAR_DRIVE_BACKWARD:
            motor = -speed;
            servo = 0;
            break;
        case CAR_DRIVE_LEFT:
            motor = speed;
            servo = 100; // 左满舵
            break;
        case CAR_DRIVE_RIGHT:
            motor = speed;
            servo = -100; // 右满舵
            break;
        default:
            motor = 0;
            servo = 0;
            break;
    }

    chassis_apply_target(motor, servo);
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
