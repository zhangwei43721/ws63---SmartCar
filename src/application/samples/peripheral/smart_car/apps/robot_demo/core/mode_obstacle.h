#ifndef MODE_OBSTACLE_H
#define MODE_OBSTACLE_H

#include "robot_mgr.h"
#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"
#include "soc_osal.h"
#include <stdio.h>
#include "../services/net_service.h"

void mode_obstacle_run(void);
void mode_obstacle_tick(void);

#endif
