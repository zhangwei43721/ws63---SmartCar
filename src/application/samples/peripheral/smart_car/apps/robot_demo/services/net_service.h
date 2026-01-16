#ifndef NET_SERVICE_H
#define NET_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "../../../drivers/wifi_client/bsp_wifi.h"

#include "osal_mutex.h"
#include "securec.h"
#include "soc_osal.h"

#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define NET_SERVICE_SERVER_IP "192.168.3.150"
#define NET_SERVICE_SERVER_PORT 8888
#define NET_SERVICE_TCP_STACK_SIZE 4096
#define NET_SERVICE_TCP_TASK_PRIORITY 24
#define NET_SERVICE_TCP_RECONNECT_DELAY_MS 2000
#define NET_SERVICE_RECV_TIMEOUT_MS 100

#define NET_SERVICE_WIFI_RETRY_INTERVAL_MS 5000
void net_service_init(void);
bool net_service_is_connected(void);
const char *net_service_get_ip(void);
bool net_service_pop_cmd(int8_t *motor_out, int8_t *servo1_out, int8_t *servo2_out);
void net_service_push_cmd(int8_t motor, int8_t servo1, int8_t servo2);
bool net_service_send_text(const char *text);

#endif
