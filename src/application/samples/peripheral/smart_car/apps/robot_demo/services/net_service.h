#ifndef NET_SERVICE_H
#define NET_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

void net_service_init(void);
bool net_service_is_connected(void);
const char *net_service_get_ip(void);
bool net_service_pop_cmd(uint8_t *cmd_out, uint8_t *val_out);
bool net_service_send_text(const char *text);

#endif
