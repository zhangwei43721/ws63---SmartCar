#ifndef BSP_MOTOR_H
#define BSP_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

// 电机控制 BSP 驱动。内部创建 motor_exec 任务（优先级 10），
// 消息队列消费 int8_t[2]（left, right），L9110S 硬件输出 + 400ms 看门狗自动停车。
//
// 调用规约：只允许 apps/car_demo/core/ 下的代码调用本接口
// （mode_trace / mode_obstacle / car_ctrl 安全网关）。
// channels/ 通道层禁止直触，必须经 car_ctrl_manual_drive() 仲裁。
void bsp_motor_init(void);

// 推入电机命令。-100~100 正负值对应正反转/差速。
bool bsp_motor_push_cmd(int8_t left, int8_t right);

#endif
