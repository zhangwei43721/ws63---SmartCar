#ifndef BSP_WIFI_AP_H
#define BSP_WIFI_AP_H

#include <stdbool.h>

/* AP 配置 */
#define BSP_WIFI_AP_SSID     "WS63_Robot"
#define BSP_WIFI_AP_PASSWORD ""
#define BSP_WIFI_AP_CHANNEL  13

/* ========== AP Task 接口 ========== */
bool ap_task_start(void);
void ap_task_stop(void);
void ap_set_should_exit(bool v);
int ap_task_main(void* arg);

#endif
