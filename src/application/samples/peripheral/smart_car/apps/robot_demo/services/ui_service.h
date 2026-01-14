#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include "../robot_common.h"

void ui_service_init(void);
void ui_show_mode_page(CarStatus status);
void ui_render_standby(const char *wifi_state, const char *ip_addr);

#endif
