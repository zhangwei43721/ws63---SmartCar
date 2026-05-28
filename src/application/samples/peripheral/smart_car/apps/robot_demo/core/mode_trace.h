#ifndef MODE_TRACE_H
#define MODE_TRACE_H

void mode_trace_enter(void); /* 进入循迹模式（创建任务 + 启动传感器） */
void mode_trace_exit(void);  /* 退出循迹模式（通知任务停止 + 等待退出） */

/* 设置循迹 PID 参数 (Kp/Ki/Kd/BaseSpeed)，值为放大100倍的整数，speed 为原值 */
void mode_trace_set_pid(int type, int value);

/**
 * @brief 显式保存当前 PID 参数到 NV 存储
 * @note 不再在 set_pid 中自动保存，改由前端发送保存命令时调用
 */
void mode_trace_save_pid(void);

#endif
