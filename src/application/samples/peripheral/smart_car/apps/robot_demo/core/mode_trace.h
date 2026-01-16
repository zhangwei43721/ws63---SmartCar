#ifndef MODE_TRACE_H
#define MODE_TRACE_H

#include "robot_mgr.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include "soc_osal.h"

#include <stdio.h>
#include "../services/net_service.h"
void mode_trace_run(void);
void mode_trace_tick(void);

#endif
