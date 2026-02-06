#ifndef ROBOT_MGR_H
#define ROBOT_MGR_H

#include <stdbool.h>

#include "../robot_common.h"

// 模式接口定义
typedef struct {
  void (*enter)(void);  // 进入模式时调用（初始化）
  void (*tick)(void);   // 模式周期性调用（主循环）
  void (*exit)(void);   // 退出模式时调用（清理）
} RobotModeOps;

void robot_mgr_init(void);
CarStatus robot_mgr_get_status(void);
void robot_mgr_set_status(CarStatus status);

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
