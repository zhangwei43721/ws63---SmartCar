#include "udp_net_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "lwip/inet.h"
#include "lwip/netifapi.h"
#include "securec.h"
#include "soc_osal.h"

static osal_mutex g_net_mutex;
static bool g_net_mutex_inited = false;

#define NET_LOCK() MUTEX_LOCK(g_net_mutex, g_net_mutex_inited)
#define NET_UNLOCK() MUTEX_UNLOCK(g_net_mutex, g_net_mutex_inited)

bool g_udp_net_wifi_connected = false;
bool g_udp_net_wifi_has_ip = false;
char g_udp_net_ip[BUF_IP] = "0.0.0.0";

int g_udp_net_socket_fd = -1;
bool g_udp_net_bound = false;

//* @brief 8位累加校验和
uint8_t udp_net_common_checksum8_add(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    if (data && len) {
        for (size_t i = 0; i < len; i++)
            sum += data[i];
    }
    return sum;
}

/**
 * @brief 获取本机 WiFi MAC 地址
 * @param mac_buf 输出的 MAC 地址缓冲区（至少 6 字节）
 * @return 成功返回 0，失败返回 -1
 * @note 优先从 wlan0 获取，失败则尝试 ap0（AP 模式）
 */
int udp_net_get_mac_address(uint8_t *mac_buf)
{
    if (!mac_buf)
        return -1;

    struct netif *netif_p = netifapi_netif_find("wlan0");
    if (netif_p == NULL)
        netif_p = netifapi_netif_find("ap0");

    if (netif_p) {
        memcpy_s(mac_buf, 6, netif_p->hwaddr, 6);
        return 0;
    }
    return -1;
}

// 内部辅助：发送数据到指定地址
static int send_udp_raw(const void *buf, size_t len, const struct sockaddr_in *addr)
{
    if (!buf || len == 0 || !addr)
        return -1;
    NET_LOCK();
    int fd = g_udp_net_socket_fd;
    bool bound = g_udp_net_bound;
    NET_UNLOCK();
    return (bound && fd >= 0) ? (int)lwip_sendto(fd, buf, len, 0, (struct sockaddr *)addr, sizeof(*addr)) : -1;
}

//* @brief 向指定端口发送 UDP 广播包
int udp_net_common_send_broadcast(const void *buf, size_t len, uint16_t port)
{
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);
    return send_udp_raw(buf, len, &addr);
}

//* @brief 向指定地址发送 UDP 数据包
int udp_net_common_send_to_addr(const void *buf, size_t len, const struct sockaddr_in *addr)
{
    return send_udp_raw(buf, len, addr);
}

//* @brief 初始化网络模块互斥锁
void udp_net_common_init(void)
{
    if (!g_net_mutex_inited && osal_mutex_init(&g_net_mutex) == OSAL_SUCCESS) {
        g_net_mutex_inited = true;
    }
}

//* @brief 创建 UDP 套接字并绑定到指定端口
int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms, bool enable_broadcast)
{
    int sockfd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        return -1;

    struct timeval tv = {0, (int)recv_timeout_ms * 1000};
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (enable_broadcast) {
        int broadcast = 1;
        lwip_setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    addr.sin_addr.s_addr = IPADDR_ANY;

    if (lwip_bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("udp_net: Bind failed %d\r\n", errno);
        lwip_close(sockfd);
        return -1;
    }

    NET_LOCK();
    g_udp_net_socket_fd = sockfd;
    g_udp_net_bound = true;
    NET_UNLOCK();
    return sockfd;
}
