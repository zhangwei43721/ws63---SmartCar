#include "udp_net_common.h"
#include "../../../drivers/wifi_client/bsp_wifi.h"

#include "securec.h"

#include "lwip/inet.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static osal_mutex g_net_mutex;
static bool g_net_mutex_inited = false;

#define NET_LOCK()                               \
    do {                                         \
        if (g_net_mutex_inited)                  \
            (void)osal_mutex_lock(&g_net_mutex); \
    } while (0)

#define NET_UNLOCK()                         \
    do {                                     \
        if (g_net_mutex_inited)              \
            osal_mutex_unlock(&g_net_mutex); \
    } while (0)

static bool g_wifi_inited = false;
bool g_udp_net_wifi_connected = false;
bool g_udp_net_wifi_has_ip = false;
char g_udp_net_ip[IP_BUFFER_SIZE] = "0.0.0.0";
static unsigned int g_wifi_last_retry = 0;

int g_udp_net_socket_fd = -1;
bool g_udp_net_bound = false;

uint8_t udp_net_common_checksum8_add(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    if (data == NULL || len == 0)
        return 0;

    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

int udp_net_common_send_broadcast(const void *buf, size_t len, uint16_t port)
{
    if (buf == NULL || len == 0)
        return -1;

    NET_LOCK();
    int fd = g_udp_net_socket_fd;
    bool bound = g_udp_net_bound;
    NET_UNLOCK();
    if (!bound || fd < 0)
        return -1;

    struct sockaddr_in broadcast_addr;
    (void)memset_s(&broadcast_addr, sizeof(broadcast_addr), 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = lwip_htons(port);
    broadcast_addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);

    return (int)lwip_sendto(fd, buf, len, 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
}

/**
 * @brief 向指定地址发送 UDP 数据（非广播）
 * @note 依赖 udp_net_common_open_and_bind() 已成功创建并绑定套接字
 */
int udp_net_common_send_to_addr(const void *buf, size_t len, const struct sockaddr_in *addr)
{
    if (buf == NULL || len == 0 || addr == NULL)
        return -1;

    NET_LOCK();
    int fd = g_udp_net_socket_fd;
    bool bound = g_udp_net_bound;
    NET_UNLOCK();
    if (!bound || fd < 0)
        return -1;

    return (int)lwip_sendto(fd, buf, len, 0, (const struct sockaddr *)addr, sizeof(*addr));
}

void udp_net_common_init(void)
{
    if (g_net_mutex_inited)
        return;

    if (osal_mutex_init(&g_net_mutex) == OSAL_SUCCESS)
        g_net_mutex_inited = true;
}

int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms, bool enable_broadcast)
{
    int sockfd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("udp_net: 套接字创建失败，错误码=%d\r\n", errno);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = (int)recv_timeout_ms * 1000;
    (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (enable_broadcast) {
        int broadcast = 1;
        (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    }

    struct sockaddr_in addr;
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    addr.sin_addr.s_addr = IPADDR_ANY;

    if (lwip_bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("udp_net: 绑定失败，错误码=%d\r\n", errno);
        lwip_close(sockfd);
        return -1;
    }

    NET_LOCK();
    g_udp_net_socket_fd = sockfd;
    g_udp_net_bound = true;
    NET_UNLOCK();

    return sockfd;
}

void udp_net_common_wifi_ensure_connected(void)
{
    unsigned long long now_jiffies = osal_get_jiffies();
    unsigned int now = (unsigned int)now_jiffies;

    if (!g_wifi_inited) {
        if (bsp_wifi_init() != 0)
            return;

        g_wifi_inited = true;
        g_wifi_last_retry = 0;
    }

    if (!g_udp_net_wifi_connected) {
        if (g_wifi_last_retry == 0 || (now - g_wifi_last_retry >= 5000)) {
            g_wifi_last_retry = now;
            if (bsp_wifi_connect_default() == 0)
                g_udp_net_wifi_connected = true;
        }
    }

    if (g_udp_net_wifi_connected) {
        bsp_wifi_status_t status = bsp_wifi_get_status();
        if (status == BSP_WIFI_STATUS_GOT_IP) {
            if (bsp_wifi_get_ip(g_udp_net_ip, sizeof(g_udp_net_ip)) == 0)
                g_udp_net_wifi_has_ip = true;
        } else if (status == BSP_WIFI_STATUS_DISCONNECTED) {
            g_udp_net_wifi_connected = false;
            g_udp_net_wifi_has_ip = false;
        }
    }
}
