#ifndef NET_SERVICE_H
#define NET_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

void net_service_init(void);
bool net_service_is_connected(void);
const char *net_service_get_ip(void);
bool net_service_pop_cmd(int8_t *motor_out, int8_t *servo1_out, int8_t *servo2_out);
void net_service_push_cmd(int8_t motor, int8_t servo1, int8_t servo2);
bool net_service_send_text(const char *text);

#endif
