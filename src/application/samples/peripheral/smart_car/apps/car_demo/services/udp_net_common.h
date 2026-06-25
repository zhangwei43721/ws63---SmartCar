#ifndef UDP_NET_COMMON_H
#define UDP_NET_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
int udp_net_common_open_and_bind(uint16_t port,
                                 unsigned int recv_timeout_ms,
                                 bool enable_broadcast);                               /* 创建 UDP socket 并绑定端口 */
int udp_net_common_send_to_addr(int fd,
                                const void *buf,
                                size_t len,
                                const struct sockaddr_in *addr); /* 向指定地址发送 UDP 数据 */

void http_send_response_and_close(int client_fd, const char *response); /* 发送 HTTP 响应并关闭连接 */

#endif
