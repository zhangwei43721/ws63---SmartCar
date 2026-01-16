#include "net_service.h"
#include "../core/robot_config.h"

static void *net_service_tcp_task(const char *arg);
static void net_service_wifi_ensure_connected(void);
static int net_service_read_frame(int sockfd, uint8_t frame_out[4]);
static void net_service_handle_frame(const uint8_t frame[4]);

static osal_task *g_tcp_task_handle = NULL;
static bool g_tcp_task_started = false;

static osal_mutex g_mutex;
static bool g_mutex_inited = false;

// =============== 互斥锁操作宏 ===============
#define NET_LOCK() \
    do { \
        if (g_mutex_inited) \
            (void)osal_mutex_lock(&g_mutex); \
    } while(0)

#define NET_UNLOCK() \
    do { \
        if (g_mutex_inited) \
            osal_mutex_unlock(&g_mutex); \
    } while(0)

static bool g_wifi_inited = false;
static bool g_wifi_connected = false;
static bool g_wifi_has_ip = false;
static char g_ip[IP_BUFFER_SIZE] = "0.0.0.0";
static unsigned int g_wifi_last_retry = 0;

static bool g_tcp_connected = false;
static int g_tcp_socket_fd = -1;

static uint8_t g_rx_buffer[4];
static size_t g_rx_filled = 0;

static int8_t g_latest_motor = 0;
static int8_t g_latest_servo1 = 0;
static int8_t g_latest_servo2 = 0;
static bool g_has_latest = false;

/**
 * @brief 初始化网络服务互斥锁
 */
static void net_service_mutex_init(void)
{
    if (g_mutex_inited)
        return;

    if (osal_mutex_init(&g_mutex) == OSAL_SUCCESS)
        g_mutex_inited = true;
    else
        printf("net_service: 互斥锁初始化失败\r\n");
}

/**
 * @brief 初始化网络服务，创建 TCP 通信任务
 */
void net_service_init(void)
{
    if (g_tcp_task_started)
        return;

    net_service_mutex_init();

    osal_kthread_lock();
    g_tcp_task_handle = osal_kthread_create((osal_kthread_handler)net_service_tcp_task, NULL, "net_tcp_task",
                                            NET_SERVICE_TCP_STACK_SIZE);
    if (g_tcp_task_handle != NULL) {
        (void)osal_kthread_set_priority(g_tcp_task_handle, NET_SERVICE_TCP_TASK_PRIORITY);
        g_tcp_task_started = true;
        printf("net_service: TCP 任务已创建\r\n");
    } else
        printf("net_service: 创建 TCP 任务失败\r\n");

    osal_kthread_unlock();
}

/**
 * @brief 检查 TCP 连接状态
 * @return 已连接返回 true，否则返回 false
 */
bool net_service_is_connected(void)
{
    NET_LOCK();
    bool connected = g_tcp_connected;
    NET_UNLOCK();
    return connected;
}

/**
 * @brief 获取本机 IP 地址
 * @return IP 地址字符串
 */
const char *net_service_get_ip(void)
{
    return g_ip;
}

/**
 * @brief 从命令队列中取出一条控制命令
 * @param motor_out 电机命令输出指针
 * @param servo1_out 舵机1命令输出指针
 * @param servo2_out 舵机2命令输出指针
 * @return 有命令返回 true，无命令返回 false
 */
bool net_service_pop_cmd(int8_t *motor_out, int8_t *servo1_out, int8_t *servo2_out)
{
    if (motor_out == NULL || servo1_out == NULL || servo2_out == NULL)
        return false;

    NET_LOCK();
    bool has = g_has_latest;
    if (has) {
        *motor_out = g_latest_motor;
        *servo1_out = g_latest_servo1;
        *servo2_out = g_latest_servo2;
        g_has_latest = false;
    }
    NET_UNLOCK();

    return has;
}

/**
 * @brief 将控制命令推入队列
 * @param motor 电机命令值
 * @param servo1 舵机1命令值
 * @param servo2 舵机2命令值
 */
void net_service_push_cmd(int8_t motor, int8_t servo1, int8_t servo2)
{
    NET_LOCK();
    g_latest_motor = motor;
    g_latest_servo1 = servo1;
    g_latest_servo2 = servo2;
    g_has_latest = true;
    NET_UNLOCK();
}

/**
 * @brief 通过 TCP 连接发送文本数据
 * @param text 待发送的文本
 * @return 发送成功返回 true，失败返回 false
 */
bool net_service_send_text(const char *text)
{
    if (text == NULL)
        return false;

    NET_LOCK();
    int fd = g_tcp_socket_fd;
    bool connected = g_tcp_connected;
    NET_UNLOCK();

    if (!connected || fd < 0)
        return false;

    size_t len = strlen(text);
    if (len == 0)
        return true;

    ssize_t n = lwip_send(fd, text, len, 0);
    return (n == (ssize_t)len);
}

/**
 * @brief 确保 WiFi 连接并获取 IP 地址
 * @note 自动处理 WiFi 初始化、连接和重连
 */
static void net_service_wifi_ensure_connected(void)
{
    unsigned long long now_jiffies = osal_get_jiffies();
    unsigned int now = (unsigned int)now_jiffies;

    if (!g_wifi_inited) {
        if (bsp_wifi_init() != 0)
            return;

        g_wifi_inited = true;
        g_wifi_last_retry = 0;
    }

    if (!g_wifi_connected) {
        if (g_wifi_last_retry == 0 || (now - g_wifi_last_retry >= NET_SERVICE_WIFI_RETRY_INTERVAL_MS)) {
            g_wifi_last_retry = now;
            if (bsp_wifi_connect_default() == 0)
                g_wifi_connected = true;
        }
    }

    if (g_wifi_connected) {
        bsp_wifi_status_t status = bsp_wifi_get_status();
        if (status == BSP_WIFI_STATUS_GOT_IP) {
            if (bsp_wifi_get_ip(g_ip, sizeof(g_ip)) == 0)
                g_wifi_has_ip = true;
        } else if (status == BSP_WIFI_STATUS_DISCONNECTED) {
            g_wifi_connected = false;
            g_wifi_has_ip = false;
        }
    }
}

/**
 * @brief TCP 通信任务主函数
 * @param arg 任务参数（未使用）
 * @note 维护与服务器的 TCP 连接，接收控制命令
 */
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
            printf("net_service: 套接字创建失败，错误码=%d\r\n", errno);
            osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        struct sockaddr_in server_addr;
        (void)memset_s(&server_addr, sizeof(server_addr), 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = lwip_htons(NET_SERVICE_SERVER_PORT);
        server_addr.sin_addr.s_addr = ipaddr_addr(NET_SERVICE_SERVER_IP);
        if (server_addr.sin_addr.s_addr == IPADDR_NONE) {
            printf("net_service: 无效的服务器 IP 地址\r\n");
            lwip_close(sockfd);
            osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        printf("net_service: 正在连接到 %s:%u ...\r\n", NET_SERVICE_SERVER_IP, NET_SERVICE_SERVER_PORT);
        if (lwip_connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("net_service: 连接失败，错误码=%d\r\n", errno);
            lwip_close(sockfd);
            osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        struct timeval tv;
        tv.tv_sec = NET_SERVICE_RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (NET_SERVICE_RECV_TIMEOUT_MS % 1000) * 1000;
        (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        NET_LOCK();
        g_tcp_connected = true;
        g_tcp_socket_fd = sockfd;
        g_rx_filled = 0;
        NET_UNLOCK();

        printf("net_service: 已连接\r\n");

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
        NET_LOCK();
        g_tcp_connected = false;
        g_tcp_socket_fd = -1;
        g_rx_filled = 0;
        NET_UNLOCK();

        printf("net_service: 已断开连接，%u 毫秒后重试\r\n", NET_SERVICE_TCP_RECONNECT_DELAY_MS);
        osal_msleep(NET_SERVICE_TCP_RECONNECT_DELAY_MS);
    }

    return NULL;
}

/**
 * @brief 从套接字读取一个完整的 4 字节帧
 * @param sockfd 套接字文件描述符
 * @param frame_out 输出缓冲区，存储读取的 4 字节数据
 * @return 1 表示读取完整帧，0 表示数据未就绪，-1 表示连接关闭或错误
 */
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

/**
 * @brief 处理接收到的 4 字节控制帧
 * @param frame 4 字节数据帧，格式为 [motor, servo1, servo2, checksum]
 */
static void net_service_handle_frame(const uint8_t frame[4])
{
    uint8_t checksum = frame[0] ^ frame[1] ^ frame[2];
    if (checksum != frame[3])
        return;

    NET_LOCK();
    g_latest_motor = (int8_t)frame[0];
    g_latest_servo1 = (int8_t)frame[1];
    g_latest_servo2 = (int8_t)frame[2];
    g_has_latest = true;
    NET_UNLOCK();
}
