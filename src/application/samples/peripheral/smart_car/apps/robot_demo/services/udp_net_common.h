#ifndef UDP_NET_COMMON_H
#define UDP_NET_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/robot_config.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "soc_osal.h"

void udp_net_common_init(void);
int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms,
                                 bool enable_broadcast);
int udp_net_common_send_broadcast(const void* buf, size_t len, uint16_t port);

// 确保 WiFi 已初始化并尽力保持连接；连接成功后会刷新 g_udp_net_ip。
void udp_net_common_wifi_ensure_connected(void);
extern int g_udp_net_socket_fd;
extern bool g_udp_net_bound;
extern bool g_udp_net_wifi_connected;
extern bool g_udp_net_wifi_has_ip;
extern char g_udp_net_ip[BUF_IP];

// 简单累加校验（取低 8 位），用于 UDP 协议包校验。
uint8_t udp_net_common_checksum8_add(const uint8_t* data, size_t len);

// 获取本机 WiFi MAC 地址
int udp_net_get_mac_address(uint8_t* mac_buf);

// 向指定地址发送 UDP 数据（要求已 open_and_bind 成功）。
int udp_net_common_send_to_addr(const void* buf, size_t len,
                                const struct sockaddr_in* addr);

#endif
