#include "udp_service.h"
#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "../core/robot_mgr.h"

static void *udp_service_task(const char *arg);
static void udp_service_broadcast_presence(void);

static osal_task *g_udp_task_handle = NULL;
static bool g_udp_task_started = false;

static osal_mutex g_mutex;
static bool g_mutex_inited = false;

// =============== 互斥锁操作宏 ===============
#define UDP_LOCK() \
    do { \
        if (g_mutex_inited) \
            (void)osal_mutex_lock(&g_mutex); \
    } while(0)

#define UDP_UNLOCK() \
    do { \
        if (g_mutex_inited) \
            osal_mutex_unlock(&g_mutex); \
    } while(0)

static bool g_wifi_inited = false;
static bool g_wifi_connected = false;
static bool g_wifi_has_ip = false;
static bool g_ip_printed = false;  // 标记是否已打印IP
static char g_ip[IP_BUFFER_SIZE] = "0.0.0.0";
static unsigned int g_wifi_last_retry = 0;

static int g_udp_socket_fd = -1;
static bool g_udp_bound = false;

static uint8_t g_rx_buffer[UDP_BUFFER_SIZE];
static uint8_t g_tx_buffer[UDP_BUFFER_SIZE];

static int8_t g_latest_motor1 = 0;
static int8_t g_latest_motor2 = 0;
static int8_t g_latest_servo = 0;
static bool g_has_latest = false;

// 上次发送的状态，用于检测变化
static RobotState g_last_sent_state = {0};
static bool g_state_initialized = false;
static unsigned long long g_last_state_send_time = 0;

/**
 * @brief 计算校验和
 */
static uint8_t udp_service_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/**
 * @brief 初始化UDP服务互斥锁
 */
static void udp_service_mutex_init(void)
{
    if (g_mutex_inited)
        return;

    if (osal_mutex_init(&g_mutex) == OSAL_SUCCESS)
        g_mutex_inited = true;
    else
        printf("udp_service: 互斥锁初始化失败\r\n");
}

/**
 * @brief 初始化UDP服务，创建UDP通信任务
 */
void udp_service_init(void)
{
    if (g_udp_task_started)
        return;

    udp_service_mutex_init();

    osal_kthread_lock();
    g_udp_task_handle = osal_kthread_create((osal_kthread_handler)udp_service_task, NULL, "udp_task",
                                            UDP_STACK_SIZE);
    if (g_udp_task_handle != NULL) {
        (void)osal_kthread_set_priority(g_udp_task_handle, UDP_TASK_PRIORITY);
        g_udp_task_started = true;
        printf("udp_service: UDP 任务已创建\r\n");
    } else
        printf("udp_service: 创建 UDP 任务失败\r\n");

    osal_kthread_unlock();
}

/**
 * @brief 检查UDP绑定状态
 * @return 已绑定返回 true，否则返回 false
 */
bool udp_service_is_connected(void)
{
    UDP_LOCK();
    bool bound = g_udp_bound;
    UDP_UNLOCK();
    return bound;
}

/**
 * @brief 获取本机 IP 地址
 * @return IP 地址字符串
 */
const char *udp_service_get_ip(void)
{
    return g_ip;
}

/**
 * @brief 从命令队列中取出一条控制命令
 * @param motor1_out 左电机命令输出指针
 * @param motor2_out 右电机命令输出指针
 * @param servo_out 舵机命令输出指针
 * @return 有命令返回 true，无命令返回 false
 */
bool udp_service_pop_cmd(int8_t *motor1_out, int8_t *motor2_out, int8_t *servo_out)
{
    if (motor1_out == NULL || motor2_out == NULL || servo_out == NULL)
        return false;

    UDP_LOCK();
    bool has = g_has_latest;
    if (has) {
        *motor1_out = g_latest_motor1;
        *motor2_out = g_latest_motor2;
        *servo_out = g_latest_servo;
        g_has_latest = false;
    }
    UDP_UNLOCK();

    return has;
}

/**
 * @brief 将控制命令推入队列
 * @param motor1 左电机命令值
 * @param motor2 右电机命令值
 * @param servo 舵机命令值
 */
void udp_service_push_cmd(int8_t motor1, int8_t motor2, int8_t servo)
{
    UDP_LOCK();
    g_latest_motor1 = motor1;
    g_latest_motor2 = motor2;
    g_latest_servo = servo;
    g_has_latest = true;
    UDP_UNLOCK();
}

/**
 * @brief 检测状态是否发生变化
 */
static bool has_state_changed(const RobotState *new_state)
{
    if (!g_state_initialized)
        return true;

    return (new_state->mode != g_last_sent_state.mode ||
            new_state->servo_angle != g_last_sent_state.servo_angle ||
            (new_state->distance > 0 && new_state->distance != g_last_sent_state.distance) ||
            new_state->ir_left != g_last_sent_state.ir_left ||
            new_state->ir_middle != g_last_sent_state.ir_middle ||
            new_state->ir_right != g_last_sent_state.ir_right);
}

/**
 * @brief 发送状态数据包
 */
static void send_state_packet(const RobotState *state)
{
    UDP_LOCK();
    int fd = g_udp_socket_fd;
    bool bound = g_udp_bound;
    UDP_UNLOCK();

    if (!bound || fd < 0)
        return;

    udp_packet_t *pkt = (udp_packet_t *)g_tx_buffer;
    pkt->type = 0x02;  // 传感器状态
    pkt->cmd = (uint8_t)state->mode;
    pkt->motor1 = (int8_t)state->servo_angle;
    pkt->motor2 = (int8_t)(state->distance * 10);  // 距离放大10倍
    pkt->servo = 0;

    // 红外数据打包到 ir_data (bit0=左, bit1=中, bit2=右)
    pkt->ir_data = ((state->ir_left & 1) << 0) |
                   ((state->ir_middle & 1) << 1) |
                   ((state->ir_right & 1) << 2);

    pkt->checksum = udp_service_checksum((uint8_t *)pkt, sizeof(udp_packet_t) - 1);

    // 广播发送
    struct sockaddr_in broadcast_addr;
    (void)memset_s(&broadcast_addr, sizeof(broadcast_addr), 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = lwip_htons(UDP_BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);

    (void)lwip_sendto(fd, g_tx_buffer, sizeof(udp_packet_t), 0,
                      (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

    // 更新上次发送的状态和时间
    g_last_sent_state = *state;
    g_state_initialized = true;
    g_last_state_send_time = osal_get_jiffies();
}

/**
 * @brief 发送心跳包
 */
static void send_heartbeat(void)
{
    UDP_LOCK();
    int fd = g_udp_socket_fd;
    bool bound = g_udp_bound;
    UDP_UNLOCK();

    if (!bound || fd < 0)
        return;

    udp_packet_t *pkt = (udp_packet_t *)g_tx_buffer;
    pkt->type = 0xFE;  // 心跳包
    pkt->cmd = 0x00;
    pkt->motor1 = 0;
    pkt->motor2 = 0;
    pkt->servo = 0;
    pkt->ir_data = 0;
    pkt->checksum = udp_service_checksum((uint8_t *)pkt, sizeof(udp_packet_t) - 1);

    struct sockaddr_in broadcast_addr;
    (void)memset_s(&broadcast_addr, sizeof(broadcast_addr), 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = lwip_htons(UDP_BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);

    (void)lwip_sendto(fd, g_tx_buffer, sizeof(udp_packet_t), 0,
                      (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
}

/**
 * @brief 检查并发送状态变化
 * @note 状态变化时立即发送，否则每500ms发送一次（保持前端状态同步）
 */
void udp_service_send_state(void)
{
    RobotState state;
    robot_mgr_get_state_copy(&state);

    // 检查状态是否变化，或者超过500ms没发送了
    unsigned long long now = osal_get_jiffies();
    bool should_send = has_state_changed(&state) ||
                       (g_state_initialized && (now - g_last_state_send_time >= 500));

    if (should_send) {
        send_state_packet(&state);
    }
}

/**
 * @brief 确保 WiFi 连接并获取 IP 地址
 * @note 自动处理 WiFi 初始化、连接和重连
 */
static void udp_service_wifi_ensure_connected(void)
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
        if (g_wifi_last_retry == 0 || (now - g_wifi_last_retry >= 5000)) {
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
 * @brief 广播设备存在信息
 */
static void udp_service_broadcast_presence(void)
{
    UDP_LOCK();
    int fd = g_udp_socket_fd;
    bool bound = g_udp_bound;
    UDP_UNLOCK();

    if (!bound || fd < 0)
        return;

    // 创建存在广播包
    udp_packet_t *pkt = (udp_packet_t *)g_tx_buffer;
    pkt->type = 0xFF;  // 存在广播
    pkt->cmd = 0x00;
    pkt->motor1 = 0;
    pkt->motor2 = 0;
    pkt->servo = 0;
    pkt->ir_data = 0;
    pkt->checksum = udp_service_checksum((uint8_t *)pkt, sizeof(udp_packet_t) - 1);

    // 广播发送
    struct sockaddr_in broadcast_addr;
    (void)memset_s(&broadcast_addr, sizeof(broadcast_addr), 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = lwip_htons(UDP_BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);

    (void)lwip_sendto(fd, g_tx_buffer, sizeof(udp_packet_t), 0,
                      (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
}

/**
 * @brief UDP 通信任务主函数
 * @param arg 任务参数（未使用）
 * @note 监听UDP端口，接收控制命令
 */
static void *udp_service_task(const char *arg)
{
    UNUSED(arg);

    // 创建UDP套接字
    int sockfd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("udp_service: 套接字创建失败，错误码=%d\r\n", errno);
        return NULL;
    }

    // 设置接收超时，防止 recvfrom 阻塞导致心跳包无法发送
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;  // 100ms 超时
    (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 设置广播权限
    int broadcast = 1;
    (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // 绑定到本地端口
    struct sockaddr_in addr;
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(UDP_SERVER_PORT);
    addr.sin_addr.s_addr = IPADDR_ANY;

    if (lwip_bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("udp_service: 绑定失败，错误码=%d\r\n", errno);
        lwip_close(sockfd);
        return NULL;
    }

    UDP_LOCK();
    g_udp_socket_fd = sockfd;
    g_udp_bound = true;
    UDP_UNLOCK();

    printf("udp_service: UDP 服务已启动，监听端口 %d\r\n", UDP_SERVER_PORT);

    // 广播计数器
    unsigned long long last_broadcast = 0;
    unsigned long long last_heartbeat = 0;

    while (1) {
        // 确保WiFi连接
        udp_service_wifi_ensure_connected();

        // 打印WiFi和IP状态（只打印一次）
        if (g_wifi_connected && g_wifi_has_ip && !g_ip_printed) {
            if (strlen(g_ip) > 0 && g_ip[0] != '0') {
                printf("udp_service: WiFi已连接，IP: %s\r\n", g_ip);
                g_ip_printed = true;  // 标记已打印
            }
        } else if (!g_wifi_connected || !g_wifi_has_ip) {
            // 断开连接时重置打印标记
            g_ip_printed = false;
        }

        if (g_wifi_connected && g_wifi_has_ip) {
            unsigned long long now_jiffies = osal_get_jiffies();

            // 每2秒广播一次存在信息
            if (now_jiffies - last_broadcast >= 2000) {
                udp_service_broadcast_presence();
                last_broadcast = now_jiffies;
            }

            // 每10秒发送心跳包
            if (now_jiffies - last_heartbeat >= 10000) {
                send_heartbeat();
                last_heartbeat = now_jiffies;
            }

            // 检查状态变化并立即发送
            udp_service_send_state();
        }

        // 接收数据 (由于设置了超时，这里不会永久阻塞)
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t n = lwip_recvfrom(sockfd, g_rx_buffer, sizeof(g_rx_buffer), 0,
                                 (struct sockaddr *)&from_addr, &from_len);

        if (n > 0) {
            // 检查数据包完整性
            if ((size_t)n >= sizeof(udp_packet_t)) {
                udp_packet_t *pkt = (udp_packet_t *)g_rx_buffer;

                // 验证校验和
                uint8_t checksum = udp_service_checksum((uint8_t *)pkt, sizeof(udp_packet_t) - 1);
                if (checksum == pkt->checksum) {
                    // 处理控制命令
                    if (pkt->type == 0x01) {  // 控制命令
                        udp_service_push_cmd(pkt->motor1, pkt->motor2, pkt->servo);
                        printf("udp_service: 收到控制命令 m1=%d m2=%d s=%d\r\n",
                               pkt->motor1, pkt->motor2, pkt->servo);
                    }
                    // 处理模式切换
                    else if (pkt->type == 0x03) {  // 模式切换
                        // cmd 直接对应 CarStatus 枚举值
                        // 0=STOP, 1=TRACE, 2=AVOID, 3=WIFI, 4=BT
                        if (pkt->cmd >= 0 && pkt->cmd <= 4) {
                            printf("udp_service: 切换模式 cmd=%d (CarStatus)\r\n", pkt->cmd);
                            robot_mgr_set_status((CarStatus)pkt->cmd);
                        } else {
                            printf("udp_service: 无效的模式命令 cmd=%d\r\n", pkt->cmd);
                        }
                    }
                }
            }
        }
        // n == 0 表示超时，这是正常的，继续循环

        osal_msleep(50);
    }

    return NULL;
}