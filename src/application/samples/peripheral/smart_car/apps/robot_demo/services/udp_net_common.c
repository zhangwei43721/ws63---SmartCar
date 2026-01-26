#include "udp_net_common.h"
#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "storage_service.h"

#include "securec.h"

#include "lwip/inet.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static osal_mutex g_net_mutex;          /* 保护网络状态的互斥锁 */
static bool g_net_mutex_inited = false; /* 互斥锁是否已初始化 */

// 使用 robot_config.h 中的通用锁宏
#define NET_LOCK()    MUTEX_LOCK(g_net_mutex, g_net_mutex_inited)
#define NET_UNLOCK()  MUTEX_UNLOCK(g_net_mutex, g_net_mutex_inited)

static bool g_wifi_inited = false;             /* WiFi 是否已初始化 */
bool g_udp_net_wifi_connected = false;         /* WiFi 是否已连接（导出供其他模块使用） */
bool g_udp_net_wifi_has_ip = false;            /* WiFi 是否已获取IP（导出供其他模块使用） */
char g_udp_net_ip[BUF_IP] = "0.0.0.0"; /* 本机IP地址字符串（导出供其他模块使用） */
static unsigned int g_wifi_last_retry = 0;     /* 上次WiFi重连时间（滴答数） */

int g_udp_net_socket_fd = -1; /* UDP 套接字文件描述符（导出供其他模块使用） */
bool g_udp_net_bound = false; /* UDP 套接字是否已绑定（导出供其他模块使用） */

/**
 * @brief UDP 校验和计算（累加法）
 * @param data 数据指针
 * @param len 数据长度
 * @return 8 位校验和（累加结果取低 8 位）
 * @note 简单累加校验，适合检测明显的传输错误
 */
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

/**
 * @brief 发送 UDP 广播数据包
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @param port 目标端口
 * @return 发送的字节数，-1 表示失败
 * @note 向 255.255.255.255:port 发送广播数据
 */
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

/**
 * @brief 初始化 UDP 网络模块
 * @note 初始化互斥锁，如果已经初始化过则跳过
 */
void udp_net_common_init(void)
{
    if (g_net_mutex_inited)
        return;

    if (osal_mutex_init(&g_net_mutex) == OSAL_SUCCESS)
        g_net_mutex_inited = true;
}

/**
 * @brief 创建并绑定 UDP 套接字
 * @param port 监听端口
 * @param recv_timeout_ms 接收超时时间（毫秒）
 * @param enable_broadcast 是否启用广播
 * @return 套接字文件描述符，-1 表示失败
 * @note 创建 UDP 套接字，绑定到指定端口，设置接收超时和广播选项
 */
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

/**
 * @brief 从 NV 存储 WiFi 配置并连接
 * @return 0 成功，-1 失败
 */
static int wifi_connect_from_nv(void)
{
    char ssid[32];
    char password[64];

    storage_service_get_wifi_config(ssid, password);

    // 如果 NV 中的 SSID 为空，使用默认配置
    if (strlen(ssid) == 0) {
        return bsp_wifi_connect_ap(BSP_WIFI_SSID, BSP_WIFI_PASSWORD);
    }

    return bsp_wifi_connect_ap(ssid, password);
}

/**
 * @brief 确保 WiFi 已连接，并自动重连
 * @note 定期检查 WiFi 连接状态，如果断开则尝试重连
 *       连接成功后会更新 g_udp_net_ip 全局变量
 */
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
            if (wifi_connect_from_nv() == 0)
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
