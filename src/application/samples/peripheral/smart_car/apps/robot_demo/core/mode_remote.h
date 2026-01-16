#ifndef MODE_REMOTE_H
#define MODE_REMOTE_H

#include "robot_mgr.h"

#include "../services/net_service.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"

#include "soc_osal.h"

#include <stdio.h>
void mode_remote_run(void);
void mode_remote_tick(void);

#endif
