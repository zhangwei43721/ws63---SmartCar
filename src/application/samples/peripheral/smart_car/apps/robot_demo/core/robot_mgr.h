#ifndef ROBOT_MGR_H
#define ROBOT_MGR_H

#include "../robot_common.h"

void robot_mgr_init(void);
CarStatus robot_mgr_get_status(void);
void robot_mgr_set_status(CarStatus status);
void robot_mgr_process_loop(void);

// 状态更新接口
void robot_mgr_update_servo_angle(unsigned int angle);
void robot_mgr_update_distance(float distance);
void robot_mgr_update_ir_status(unsigned int left, unsigned int middle, unsigned int right);
void robot_mgr_get_state_copy(RobotState *out);

// 获取避障阈值（cm）。优先读取 NV 配置，异常时回退到编译期默认值。
unsigned int robot_mgr_get_obstacle_threshold_cm(void);
// 获取舵机回中角度（0~180）。优先读取 NV 配置，异常时回退到编译期默认值。
unsigned int robot_mgr_get_servo_center_angle(void);

#endif
