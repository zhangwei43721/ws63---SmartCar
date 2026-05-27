#ifndef UDP_NET_COMMON_H
#define UDP_NET_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms, bool enable_broadcast);
int udp_net_common_send_broadcast(int fd, const void *buf, size_t len, uint16_t port);
int udp_net_common_send_to_addr(int fd, const void *buf, size_t len, const struct sockaddr_in *addr);

int udp_net_get_mac_address(uint8_t *mac_buf);
void http_send_response_and_close(int client_fd, const char *response);

#endif
