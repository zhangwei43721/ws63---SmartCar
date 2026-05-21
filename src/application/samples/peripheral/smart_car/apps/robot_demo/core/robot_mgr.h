#ifndef ROBOT_MGR_H
#define ROBOT_MGR_H

#include <stdbool.h>
#include <stdint.h>

#include "../robot_common.h"

// 模式命令来源
typedef enum {
  MODE_SRC_BUTTON = 0x01,   /* 按键 ISR */
  MODE_SRC_UDP    = 0x02,   /* WiFi UDP 遥控 */
  MODE_SRC_HTTP   = 0x03,   /* 强制门户 HTTP */
  MODE_SRC_SLE    = 0x04,   /* 星闪遥控 */
  MODE_SRC_VOICE  = 0x05,   /* UART/语音命令 */
  MODE_SRC_INTERNAL = 0x06, /* 内部初始化等 */
} ModeCmdSource;

// 模式接口定义
typedef struct {
  void (*enter)(void);  // 进入模式时调用（初始化）
  void (*tick)(void);   // 模式周期性调用（主循环）
  void (*exit)(void);   // 退出模式时调用（清理）
} RobotModeOps;

void robot_mgr_init(void);
CarStatus robot_mgr_get_status(void);

/**
 * @brief 向状态机投递模式切换请求（生产者-消费者模型）。
 *        ISR / 任意任务上下文均可调用，全程非阻塞，队列满则覆盖最旧请求。
 *        实际状态切换、UI 刷新、enter/exit 全部由 robot_mgr 任务在 tick 中执行。
 * @param status 目标模式
 * @param source 来源标识（见 MODE_SRC_*），用于日志/调试
 * @return 投递是否成功
 */
bool robot_mgr_post_mode(CarStatus status, uint32_t source);

/**
 * @brief 周期性调用函数，处理模式生命周期和状态机
 */
void robot_mgr_tick(void);

// 状态查询接口
void robot_mgr_get_state_copy(RobotState* out);

// 状态更新接口（线程安全）
void robot_mgr_update_distance(float distance);
void robot_mgr_update_ir_status(unsigned int left, unsigned int middle,
                                unsigned int right);

#endif
