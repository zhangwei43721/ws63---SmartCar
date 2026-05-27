#ifndef BSP_WIFI_AP_H
#define BSP_WIFI_AP_H

#include <stdbool.h>

// ========== AP Task 接口 ==========
bool ap_task_start(void);
void ap_task_stop(void);

#endif
