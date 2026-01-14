#ifndef ROBOT_MGR_H
#define ROBOT_MGR_H

#include "../robot_common.h"

void robot_mgr_init(void);
CarStatus robot_mgr_get_status(void);
void robot_mgr_set_status(CarStatus status);
void robot_mgr_process_loop(void);

#endif
