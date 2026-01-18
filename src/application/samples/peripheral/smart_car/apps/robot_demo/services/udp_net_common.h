#ifndef UDP_NET_COMMON_H
#define UDP_NET_COMMON_H

#include "../core/robot_config.h"

#include "soc_osal.h"

#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void udp_net_common_init(void);
int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms, bool enable_broadcast);
int udp_net_common_send_broadcast(const void *buf, size_t len, uint16_t port);

void udp_net_common_wifi_ensure_connected(void);
extern int g_udp_net_socket_fd;
extern bool g_udp_net_bound;
extern bool g_udp_net_wifi_connected;
extern bool g_udp_net_wifi_has_ip;
extern char g_udp_net_ip[IP_BUFFER_SIZE];

uint8_t udp_net_common_checksum8_add(const uint8_t *data, size_t len);

#endif
