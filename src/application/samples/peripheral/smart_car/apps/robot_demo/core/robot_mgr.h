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

#endif
