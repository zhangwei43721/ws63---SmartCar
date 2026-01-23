#include "udp_service.h"
#include "../core/robot_mgr.h"
#include "../core/mode_trace.h"
#include "udp_net_common.h"
#include "ota_service.h"

#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// UDP数据包格式 (7字节)
typedef struct {
    uint8_t type;     // 数据包类型: 0x01=控制, 0x02=状态, 0x03=模式, 0xFE=心跳, 0xFF=存在广播
    uint8_t cmd;      // 命令类型/模式编号
    int8_t motor1;    // 左电机值 -100~100
    int8_t motor2;    // 右电机值 -100~100
    int8_t servo;     // 舵机角度 0~180
    int8_t ir_data;   // 红外传感器数据 (bit0=左, bit1=中, bit2=右)
    uint8_t checksum; // 校验和（累加和）
} __attribute__((packed)) udp_packet_t;

static void *udp_service_task(const char *arg);
static void udp_service_broadcast_presence(void);

static osal_task *g_udp_task_handle = NULL; /* UDP 服务任务句柄 */
static bool g_udp_task_started = false;     /* UDP 任务是否已启动 */

static osal_mutex g_mutex;          /* 保护内部状态的互斥锁 */
static bool g_mutex_inited = false; /* 互斥锁是否已初始化 */

// =============== 互斥锁操作宏 ===============
#define UDP_LOCK()                           \
    do {                                     \
        if (g_mutex_inited)                  \
            (void)osal_mutex_lock(&g_mutex); \
    } while (0)

#define UDP_UNLOCK()                     \
    do {                                 \
        if (g_mutex_inited)              \
            osal_mutex_unlock(&g_mutex); \
    } while (0)

static bool g_ip_printed = false; /* 标记是否已打印IP */

static uint8_t g_rx_buffer[UDP_BUFFER_SIZE]; /* UDP 接收缓冲区 */
static uint8_t g_tx_buffer[UDP_BUFFER_SIZE]; /* UDP 发送缓冲区 */

static int8_t g_latest_motor1 = 0; /* 最新接收的左电机值 */
static int8_t g_latest_motor2 = 0; /* 最新接收的右电机值 */
static int8_t g_latest_servo = 0;  /* 最新接收的舵机值 */
static bool g_has_latest = false;  /* 是否有最新的控制命令 */

// 上次发送的状态，用于检测变化
static RobotState g_last_sent_state = {0};            /* 上次发送的状态副本 */
static bool g_state_initialized = false;              /* 状态是否已初始化 */
static unsigned long long g_last_state_send_time = 0; /* 上次发送状态的时间 */

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
    udp_net_common_init();
    ota_service_init();

    osal_kthread_lock();
    g_udp_task_handle = osal_kthread_create((osal_kthread_handler)udp_service_task, NULL, "udp_task", UDP_STACK_SIZE);
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
    return g_udp_net_bound;
}

/**
 * @brief 获取本机 IP 地址
 * @return IP 地址字符串
 */
const char *udp_service_get_ip(void)
{
    return g_udp_net_ip;
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

    return (new_state->mode != g_last_sent_state.mode || new_state->servo_angle != g_last_sent_state.servo_angle ||
            (new_state->distance > 0 && new_state->distance != g_last_sent_state.distance) ||
            new_state->ir_left != g_last_sent_state.ir_left || new_state->ir_middle != g_last_sent_state.ir_middle ||
            new_state->ir_right != g_last_sent_state.ir_right);
}

/**
 * @brief 发送状态数据包
 */
static void send_state_packet(const RobotState *state)
{
    udp_packet_t *pkt = (udp_packet_t *)g_tx_buffer;
    pkt->type = 0x02; // 传感器状态
    pkt->cmd = (uint8_t)state->mode;
    pkt->motor1 = (int8_t)state->servo_angle;
    pkt->motor2 = (int8_t)(state->distance * 10); // 距离放大10倍
    pkt->servo = 0;

    // 红外数据打包到 ir_data (bit0=左, bit1=中, bit2=右)
    pkt->ir_data = ((state->ir_left & 1) << 0) | ((state->ir_middle & 1) << 1) | ((state->ir_right & 1) << 2);

    pkt->checksum = udp_net_common_checksum8_add((uint8_t *)pkt, sizeof(udp_packet_t) - 1);

    (void)udp_net_common_send_broadcast(g_tx_buffer, sizeof(udp_packet_t), UDP_BROADCAST_PORT);

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
    udp_packet_t *pkt = (udp_packet_t *)g_tx_buffer;
    pkt->type = 0xFE; // 心跳包
    pkt->cmd = 0x00;
    pkt->motor1 = 0;
    pkt->motor2 = 0;
    pkt->servo = 0;
    pkt->ir_data = 0;
    pkt->checksum = udp_net_common_checksum8_add((uint8_t *)pkt, sizeof(udp_packet_t) - 1);
    (void)udp_net_common_send_broadcast(g_tx_buffer, sizeof(udp_packet_t), UDP_BROADCAST_PORT);
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
    bool should_send = has_state_changed(&state) || (g_state_initialized && (now - g_last_state_send_time >= 500));

    if (should_send) {
        send_state_packet(&state);
    }
}

/**
 * @brief 广播设备存在信息
 */
static void udp_service_broadcast_presence(void)
{
    // 创建存在广播包
    udp_packet_t *pkt = (udp_packet_t *)g_tx_buffer;
    pkt->type = 0xFF; // 存在广播
    pkt->cmd = 0x00;
    pkt->motor1 = 0;
    pkt->motor2 = 0;
    pkt->servo = 0;
    pkt->ir_data = 0;
    pkt->checksum = udp_net_common_checksum8_add((uint8_t *)pkt, sizeof(udp_packet_t) - 1);
    (void)udp_net_common_send_broadcast(g_tx_buffer, sizeof(udp_packet_t), UDP_BROADCAST_PORT);
}

/**
 * @brief UDP 通信任务主函数
 * @param arg 任务参数（未使用）
 * @note 监听UDP端口，接收控制命令
 */
static void *udp_service_task(const char *arg)
{
    UNUSED(arg);

    int sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, 100, true);
    if (sockfd < 0) {
        return NULL;
    }

    printf("udp_service: UDP 服务已启动，监听端口 %d\r\n", UDP_SERVER_PORT);

    // 广播计数器
    unsigned long long last_broadcast = 0;
    unsigned long long last_heartbeat = 0;

    while (1) {
        // 确保WiFi连接
        udp_net_common_wifi_ensure_connected();

        // 打印WiFi和IP状态（只打印一次）
        const char *ip = g_udp_net_ip;
        if (g_udp_net_wifi_connected && g_udp_net_wifi_has_ip && !g_ip_printed) {
            if (ip != NULL && strlen(ip) > 0 && ip[0] != '0') {
                printf("udp_service: WiFi已连接，IP: %s\r\n", ip);
                g_ip_printed = true; // 标记已打印
            }
        } else if (!g_udp_net_wifi_connected || !g_udp_net_wifi_has_ip) {
            // 断开连接时重置打印标记
            g_ip_printed = false;
        }

        if (g_udp_net_wifi_connected && g_udp_net_wifi_has_ip) {
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
        ssize_t n =
            lwip_recvfrom(sockfd, g_rx_buffer, sizeof(g_rx_buffer), 0, (struct sockaddr *)&from_addr, &from_len);

        if (n > 0) {
            // 优先处理 OTA 数据包
            if (ota_service_process_packet(g_rx_buffer, (size_t)n, &from_addr)) {
                continue;
            }

            uint8_t type = g_rx_buffer[0];

            if ((type == 0x01 || type == 0x03 || type == 0x04) && (size_t)n == sizeof(udp_packet_t)) {
                udp_packet_t *pkt = (udp_packet_t *)g_rx_buffer;
                uint8_t checksum = udp_net_common_checksum8_add((uint8_t *)pkt, sizeof(udp_packet_t) - 1);
                if (checksum == pkt->checksum) {
                    if (pkt->type == 0x01) {
                        udp_service_push_cmd(pkt->motor1, pkt->motor2, pkt->servo);
                        // 降低控制命令的打印频率，或者注释掉，避免刷屏
                        // printf("udp_service: 收到控制命令 m1=%d m2=%d s=%d\r\n", pkt->motor1, pkt->motor2,
                        // pkt->servo);
                    } else if (pkt->type == 0x03) {
                        if (pkt->cmd >= 0 && pkt->cmd <= 4) {
                            printf("udp_service: 切换模式 cmd=%d (CarStatus)\r\n", pkt->cmd);
                            robot_mgr_set_status((CarStatus)pkt->cmd);
                        } else {
                            printf("udp_service: 无效的模式命令 cmd=%d\r\n", pkt->cmd);
                        }
                    } else if (pkt->type == 0x04) {
                        // PID 参数设置命令
                        // cmd: 参数类型 (1=Kp, 2=Ki, 3=Kd, 4=Speed)
                        // motor1: 高8位, motor2: 低8位 (组成 int16_t)
                        int16_t val = (int16_t)(((uint8_t)pkt->motor1 << 8) | (uint8_t)pkt->motor2);
                        printf("udp_service: 设置PID type=%d val=%d\r\n", pkt->cmd, val);
                        mode_trace_set_pid(pkt->cmd, val);
                    }
                }
            }
        }
        // n == 0 表示超时，这是正常的，继续循环

        osal_msleep(50);
    }

    return NULL;
}
