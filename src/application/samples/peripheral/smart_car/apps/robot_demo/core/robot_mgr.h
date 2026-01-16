#ifndef ROBOT_MGR_H
#define ROBOT_MGR_H

#include "../robot_common.h"
#include "mode_trace.h"
#include "mode_obstacle.h"
#include "mode_remote.h"

#include "../services/net_service.h"
#include "../services/ui_service.h"
#include "../services/http_ctrl_service.h"
#include "../services/udp_service.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include "soc_osal.h"

#include <stdio.h>

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
