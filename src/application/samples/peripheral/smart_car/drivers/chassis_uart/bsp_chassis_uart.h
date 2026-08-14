//
// @file bsp_chassis_uart.h
// @brief 新小车底盘 UART 串口通信驱动 (GPIO_15/GPIO_16 UART1, 2400波特率)
//

#ifndef BSP_CHASSIS_UART_H
#define BSP_CHASSIS_UART_H

#include <stdint.h>
#include <stdbool.h>

// 底盘控制数据包结构体 (5字节帧: 0xAA包头, 电机, 舵机1, 舵机2, 0xBB包尾)
typedef struct {
    int8_t motor_speed;  // 前后电机状态: 0=停止, 1~100=前进, -1~-100=后退
    int8_t servo1_angle; // 舵机1状态: 0=居中, 1~100=向左摆幅度, -1~-100=向右摆幅度
    int8_t servo2_angle; // 舵机2状态: 0=居中, 1~100=向左摆幅度, -1~-100=向右摆幅度 (预留/次要舵机)
} chassis_packet_t;

// 底盘数据接收回调函数类型
typedef void (*chassis_uart_rx_callback_t)(const chassis_packet_t *pkt);

//
// @brief 初始化底盘 UART 串口 (UART1, GPIO_15 TX, GPIO_16 RX, 2400波特率)
// @param rx_cb 接收数据帧合法时的回调函数 (可选，可传 NULL)
// @return 0 成功，非 0 失败
//
int bsp_chassis_uart_init(chassis_uart_rx_callback_t rx_cb);

//
// @brief 打包并发送 5 字节底盘控制指令 (格式: AA [motor] [servo1] [servo2] BB)
// @param motor_speed -100 ~ 100 (0为停止, 1~100前进, -1~-100后退)
// @param servo1_angle -100 ~ 100 (0居中, 1~100左摆, -1~-100右摆)
// @param servo2_angle -100 ~ 100 (0居中, 1~100左摆, -1~-100右摆)
// @return 0 成功，非 0 失败
//
int bsp_chassis_uart_send(int8_t motor_speed, int8_t servo1_angle, int8_t servo2_angle);

//
// @brief 发送底盘数据包结构体
// @param pkt 数据包指针
// @return 0 成功，非 0 失败
//
int bsp_chassis_uart_send_pkt(const chassis_packet_t *pkt);

//
// @brief 差速意图 → 舵机转向底盘指令（本底盘的阿克曼转向运动学）
// @param left  左轮速度 -100~100
// @param right 右轮速度 -100~100
// @note 前后电机速度 = (left+right)/2；转向舵机摆幅 = (right-left)*3（放大增益，±100 限幅）
//       本驱动内部由 TX 任务按 25ms 周期刷新帧：运动帧持续重发保持动作，
//       停车帧额外补发多次（串口单向无 ACK）确保停下，新命令可打断停车补发。
//
void bsp_chassis_uart_set_differential(int8_t left, int8_t right);

//
// @brief 从接收队列获取一帧数据 (支持 RTOS 阻塞/非阻塞)
// @param pkt 输出数据包
// @param timeout_ms 超时毫秒数 (0 为非阻塞)
// @return true 成功获取，false 超时或无数据
//
bool bsp_chassis_uart_recv(chassis_packet_t *pkt, uint32_t timeout_ms);

#endif // BSP_CHASSIS_UART_H
