#ifndef MODE_TRACE_H
#define MODE_TRACE_H

#include <stdint.h>
#include <stdbool.h>

void mode_trace_enter(void); // 进入循迹模式（创建任务 + 启动传感器）
void mode_trace_exit(void);  // 退出循迹模式（通知任务停止 + 等待退出）

// 设置循迹 PID 参数 (Kp/Ki/Kd/BaseSpeed)，值为放大100倍的整数，speed 为原值
void mode_trace_set_pid(int type, int value);

// 获取/更新活跃的循迹阈值并持久化
void mode_trace_get_thresholds(uint16_t *l, uint16_t *m, uint16_t *r);
void mode_trace_update_thresholds(uint16_t l, uint16_t m, uint16_t r);

// 设置循迹子模式 (0: PID巡线, 1: 硬编码巡线, 2: 传感器校准)
void mode_trace_set_submode(uint8_t submode);
bool mode_trace_is_calibrating(void);

#endif
