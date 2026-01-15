#include "net_service.h"

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

static void *net_service_tcp_task(const char *arg);
static void net_service_wifi_ensure_connected(void);
static int net_service_read_frame(int sockfd, uint8_t frame_out[4]);
static void net_service_handle_frame(const uint8_t frame[4]);

static osal_task *g_tcp_task_handle = NULL;
static bool g_tcp_task_started = false;

static osal_mutex g_mutex;
static bool g_mutex_inited = false;

static bool g_wifi_inited = false;
static bool g_wifi_connected = false;
static bool g_wifi_has_ip = false;
static char g_ip[32] = "0.0.0.0";
static unsigned int g_wifi_last_retry = 0;

static bool g_tcp_connected = false;
static int g_tcp_socket_fd = -1;

static uint8_t g_rx_buffer[4];
static size_t g_rx_filled = 0;

static int8_t g_latest_motor = 0;
static int8_t g_latest_servo1 = 0;
static int8_t g_latest_servo2 = 0;
static bool g_has_latest = false;

static void net_service_mutex_init(void)
{
    if (g_mutex_inited) {
        return;
    }

    if (osal_mutex_init(&g_mutex) == OSAL_SUCCESS) {
        g_mutex_inited = true;
    } else {
        printf("net_service: mutex init failed\r\n");
    }
}

static void net_service_lock(void)
{
    if (g_mutex_inited) {
        (void)osal_mutex_lock(&g_mutex);
    }
}

static void net_service_unlock(void)
{
    if (g_mutex_inited) {
        osal_mutex_unlock(&g_mutex);
    }
}

void net_service_init(void)
{
    if (g_tcp_task_started) {
        return;
    }

    net_service_mutex_init();

    osal_kthread_lock();
    g_tcp_task_handle = osal_kthread_create((osal_kthread_handler)net_service_tcp_task, NULL, "net_tcp_task",
                                            NET_SERVICE_TCP_STACK_SIZE);
    if (g_tcp_task_handle != NULL) {
        (void)osal_kthread_set_priority(g_tcp_task_handle, NET_SERVICE_TCP_TASK_PRIORITY);
        g_tcp_task_started = true;
        printf("net_service: tcp task created\r\n");
    } else {
        printf("net_service: failed to create tcp task\r\n");
    }
    osal_kthread_unlock();
}

bool net_service_is_connected(void)
{
    net_service_lock();
    bool connected = g_tcp_connected;
    net_service_unlock();
    return connected;
}

const char *net_service_get_ip(void)
{
    return g_ip;
}

bool net_service_pop_cmd(int8_t *motor_out, int8_t *servo1_out, int8_t *servo2_out)
{
    if (motor_out == NULL || servo1_out == NULL || servo2_out == NULL) {
        return false;
    }

    net_service_lock();
    bool has = g_has_latest;
    if (has) {
        *motor_out = g_latest_motor;
        *servo1_out = g_latest_servo1;
        *servo2_out = g_latest_servo2;
        g_has_latest = false;
    }
    net_service_unlock();

    return has;
}

void net_service_push_cmd(int8_t motor, int8_t servo1, int8_t servo2)
{
    net_service_lock();
    g_latest_motor = motor;
    g_latest_servo1 = servo1;
    g_latest_servo2 = servo2;
    g_has_latest = true;
    net_service_unlock();
}

bool net_service_send_text(const char *text)
{
    if (text == NULL) {
        return false;
    }

    net_service_lock();
    int fd = g_tcp_socket_fd;
    bool connected = g_tcp_connected;
    net_service_unlock();

    if (!connected || fd < 0) {
        return false;
    }

    size_t len = strlen(text);
    if (len == 0) {
        return true;
    }

    ssize_t n = lwip_send(fd, text, len, 0);
    return (n == (ssize_t)len);
}

static void net_service_wifi_ensure_connected(void)
{
    unsigned long long now_jiffies = osal_get_jiffies();
    unsigned int now = (unsigned int)now_jiffies;

    if (!g_wifi_inited) {
        if (bsp_wifi_init() != 0) {
            return;
        }
        g_wifi_inited = true;
        g_wifi_last_retry = 0;
    }

    if (!g_wifi_connected) {
        if (g_wifi_last_retry == 0 || (now - g_wifi_last_retry >= NET_SERVICE_WIFI_RETRY_INTERVAL_MS)) {
            g_wifi_last_retry = now;
            if (bsp_wifi_connect_default() == 0) {
                g_wifi_connected = true;
            }
        }
    }

    if (g_wifi_connected) {
        bsp_wifi_status_t status = bsp_wifi_get_status();
        if (status == BSP_WIFI_STATUS_GOT_IP) {
            if (bsp_wifi_get_ip(g_ip, sizeof(g_ip)) == 0) {
                g_wifi_has_ip = true;
            }
        } else if (status == BSP_WIFI_STATUS_DISCONNECTED) {
            g_wifi_connected = false;
            g_wifi_has_ip = false;
        }
    }
}

static void *net_service_tcp_task(const char *arg)
{
    UNUSED(arg);

    while (1) {
        net_service_wifi_ensure_connected();
        if (!g_wifi_connected || !g_wifi_has_ip) {
            osal_msleep(500);
            continue;
        }

        int sockfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            printf("net_service: socket create failed, err=%d\r\n", errno);
            osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        struct sockaddr_in server_addr;
        (void)memset_s(&server_addr, sizeof(server_addr), 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = lwip_htons(NET_SERVICE_SERVER_PORT);
        server_addr.sin_addr.s_addr = ipaddr_addr(NET_SERVICE_SERVER_IP);
        if (server_addr.sin_addr.s_addr == IPADDR_NONE) {
            printf("net_service: invalid server IP\r\n");
            lwip_close(sockfd);
            osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        printf("net_service: connecting to %s:%u ...\r\n", NET_SERVICE_SERVER_IP, NET_SERVICE_SERVER_PORT);
        if (lwip_connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("net_service: connect failed, err=%d\r\n", errno);
            lwip_close(sockfd);
            osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        struct timeval tv;
        tv.tv_sec = NET_SERVICE_RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (NET_SERVICE_RECV_TIMEOUT_MS % 1000) * 1000;
        (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        net_service_lock();
        g_tcp_connected = true;
        g_tcp_socket_fd = sockfd;
        g_rx_filled = 0;
        net_service_unlock();

        printf("net_service: connected\r\n");

        bool connection_alive = true;
        uint8_t frame[4];
        while (connection_alive) {
            int ret = net_service_read_frame(sockfd, frame);
            if (ret == 1) {
                net_service_handle_frame(frame);
            } else if (ret < 0) {
                connection_alive = false;
                break;
            }
            osal_msleep(5);
        }

        lwip_close(sockfd);
        net_service_lock();
        g_tcp_connected = false;
        g_tcp_socket_fd = -1;
        g_rx_filled = 0;
        net_service_unlock();

        printf("net_service: disconnected, retry after %u ms\r\n", NET_SERVICE_TCP_RECONNECT_DELAY_MS);
        osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
    }

    return NULL;
}

static int net_service_read_frame(int sockfd, uint8_t frame_out[4])
{
    while (g_rx_filled < sizeof(g_rx_buffer)) {
        ssize_t n = lwip_recv(sockfd, g_rx_buffer + g_rx_filled, sizeof(g_rx_buffer) - g_rx_filled, 0);
        if (n > 0) {
            g_rx_filled += (size_t)n;
        } else if (n == 0) {
            return -1;
        } else {
            if (errno == EAGAIN) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (g_rx_filled == sizeof(g_rx_buffer)) {
            break;
        }
    }

    if (g_rx_filled == sizeof(g_rx_buffer)) {
        memcpy_s(frame_out, 4, g_rx_buffer, 4);
        g_rx_filled = 0;
        return 1;
    }

    return 0;
}

static void net_service_handle_frame(const uint8_t frame[4])
{
    uint8_t checksum = frame[0] ^ frame[1] ^ frame[2];
    if (checksum != frame[3]) {
        return;
    }

    net_service_lock();
    g_latest_motor = (int8_t)frame[0];
    g_latest_servo1 = (int8_t)frame[1];
    g_latest_servo2 = (int8_t)frame[2];
    g_has_latest = true;
    net_service_unlock();
}
