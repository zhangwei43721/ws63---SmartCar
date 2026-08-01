#ifndef CAR_CTRL_H
#define CAR_CTRL_H

#include <stdbool.h>
#include <stdint.h>

// 控制中枢：全车唯一的"决策点"。
// 所有外部通道（UDP / SLE / HTTP / 语音）的控制意图都必须经过这里仲裁，
// channels/ 下的代码只允许调用本头文件与 mode_mgr.h，禁止直触电机驱动。
void car_ctrl_manual_drive(int8_t left, int8_t right, uint32_t source); // 手动驾驶安全网关
bool car_ctrl_is_manual_allowed(void);                                  // 当前是否允许手动驾驶

// ---------- 统一命令总线 ----------
// 二进制协议通道（UDP / SLE）不直接调处理函数，而是把原始包投递到总线，
// 由 car_ctrl 任务在统一上下文里串行消费：单点仲裁、单点日志、单点应答路由。
#define CAR_CMD_MAX_PAYLOAD 16 // 统一协议最大包长（最长的 TRACE_INFO 为 13 字节）

// 通道应答回调：中枢需要回包（如 OTA ACK）时调用，由来源通道实现"从哪来回哪去"
typedef void (*car_reply_fn)(void *ctx, const uint8_t *data, uint16_t len);

typedef struct {
    uint32_t source;   // MODE_SRC_* 命令来源
    car_reply_fn reply; // 应答回调（NULL 表示本包不需要应答能力）
    void *reply_ctx;    // 通道私有上下文，原样回传给 reply
    uint16_t len;       // data 有效长度
    uint8_t data[CAR_CMD_MAX_PAYLOAD]; // 原始协议包（投递时拷贝，通道栈缓冲可立即复用）
} car_cmd_t;

void car_ctrl_init(void);                  // 创建命令队列与处理任务（通道注册前调用）
bool car_ctrl_post_cmd(const car_cmd_t *cmd); // 投递命令到总线（拷贝，任务上下文调用）

#endif
