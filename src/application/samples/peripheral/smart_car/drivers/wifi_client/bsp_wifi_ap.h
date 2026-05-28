#ifndef BSP_WIFI_AP_H
#define BSP_WIFI_AP_H

#include <stdbool.h>

// ========== AP Task 接口 ==========
bool ap_task_start(void); /* 启动 WiFi AP 热点任务 */
void ap_task_stop(void);  /* 停止 WiFi AP 热点任务 */

#endif
