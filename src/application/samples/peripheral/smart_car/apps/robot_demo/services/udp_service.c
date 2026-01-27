#include "udp_service.h"
#include "../core/robot_mgr.h"
#include "../core/robot_config.h"
#include "../core/mode_trace.h"
#include "udp_net_common.h"
#include "ota_service.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

// --- 前向声明 ---
static void *udp_service_task(const char *arg);

// --- 数据结构 ---
typedef struct {
    uint8_t type;   // 0x01=控制, 0x02=状态, 0x03=模式, 0x04=PID, 0xFE=心跳, 0xFF=广播
    uint8_t cmd;    // 命令/模式
    int8_t motor1;  // 左电机 / PID高位
    int8_t motor2;  // 右电机 / PID低位
    int8_t servo;   // 舵机
    int8_t ir_data; // 红外
    uint8_t checksum;
} __attribute__((packed)) udp_packet_t;

// --- 全局变量 ---
static osal_mutex g_mutex;
static bool g_mutex_inited = false;
static uint8_t g_buf[UDP_BUFFER_SIZE]; // 收发共用缓冲区（单线程安全）
static struct {
    int8_t m1, m2, servo;
    bool has_new;
} g_cmd = {0}; // 命令缓存
static struct {
    RobotState last;
    uint64_t last_time;
    bool inited;
} g_state = {0}; // 状态缓存

// --- 核心辅助函数 ---

/**
 * @brief 通用发送函数：构建包、计算校验和并广播
 */
static void send_packet(uint8_t type, uint8_t cmd, int8_t m1, int8_t m2, int8_t servo, int8_t ir)
{
    udp_packet_t *pkt = (udp_packet_t *)g_buf;
    pkt->type = type;
    pkt->cmd = cmd;
    pkt->motor1 = m1;
    pkt->motor2 = m2;
    pkt->servo = servo;
    pkt->ir_data = ir;
    pkt->checksum = udp_net_common_checksum8_add((uint8_t *)pkt, 6);
    udp_net_common_send_broadcast(g_buf, 7, UDP_BROADCAST_PORT);
}

// --- 外部接口 ---

void udp_service_init(void)
{
    if (g_mutex_inited)
        return;
    udp_net_common_init();
    ota_service_init();
    if (osal_mutex_init(&g_mutex) == OSAL_SUCCESS)
        g_mutex_inited = true;

    osal_task *task = osal_kthread_create((osal_kthread_handler)udp_service_task, NULL, "udp_task", UDP_STACK_SIZE);
    if (task) {
        osal_kthread_set_priority(task, UDP_TASK_PRIORITY);
        printf("udp_service: 任务启动\r\n");
    }
}

bool udp_service_is_connected(void)
{
    return g_udp_net_wifi_connected && g_udp_net_wifi_has_ip && g_udp_net_bound;
}

// WiFi连接状态返回
WifiConnectStatus udp_service_get_wifi_status(void)
{
    if (g_udp_net_wifi_connected && g_udp_net_wifi_has_ip)
        return WIFI_STATUS_CONNECTED;
    if (g_udp_net_wifi_connected)
        return WIFI_STATUS_CONNECTING; // 已连接 WiFi 但未获取 IP
    return WIFI_STATUS_DISCONNECTED;   // 未连接
}

const char *udp_service_get_ip(void)
{
    return g_udp_net_ip;
}

// 存入命令 (线程安全)
void udp_service_push_cmd(int8_t m1, int8_t m2, int8_t s)
{
    MUTEX_LOCK(g_mutex, g_mutex_inited);
    g_cmd.m1 = m1;
    g_cmd.m2 = m2;
    g_cmd.servo = s;
    g_cmd.has_new = true;
    MUTEX_UNLOCK(g_mutex, g_mutex_inited);
}

// 取出命令 (线程安全)
bool udp_service_pop_cmd(int8_t *m1, int8_t *m2, int8_t *s)
{
    bool ret = false;
    MUTEX_LOCK(g_mutex, g_mutex_inited);
    if (g_cmd.has_new) {
        *m1 = g_cmd.m1;
        *m2 = g_cmd.m2;
        *s = g_cmd.servo;
        g_cmd.has_new = false;
        ret = true;
    }
    MUTEX_UNLOCK(g_mutex, g_mutex_inited);
    return ret;
}

// --- 内部逻辑 ---

// 检查并发送状态 (变化时发送 或 每500ms发送)
static void check_and_send_state(uint64_t now)
{
    RobotState curr;
    robot_mgr_get_state_copy(&curr);

    bool changed = !g_state.inited || curr.mode != g_state.last.mode || curr.servo_angle != g_state.last.servo_angle ||
                   (curr.distance > 0 && curr.distance != g_state.last.distance) ||
                   curr.ir_left != g_state.last.ir_left || curr.ir_middle != g_state.last.ir_middle ||
                   curr.ir_right != g_state.last.ir_right;

    if (changed || (now - g_state.last_time >= 500)) {
        uint8_t ir_bits = (curr.ir_left & 1) | ((curr.ir_middle & 1) << 1) | ((curr.ir_right & 1) << 2);
        send_packet(0x02, curr.mode, curr.servo_angle, (int8_t)(curr.distance * 10), 0, ir_bits);

        g_state.last = curr;
        g_state.last_time = now;
        g_state.inited = true;
    }
}

// --- 主任务 ---

static void *udp_service_task(const char *arg)
{
    (void)arg;
    int sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, 100, true);
    if (sockfd < 0)
        return NULL;

    uint64_t t_broadcast = 0, t_heartbeat = 0;
    bool ip_shown = false;

    while (1) {
        udp_net_common_wifi_ensure_connected();
        bool ready = g_udp_net_wifi_connected && g_udp_net_wifi_has_ip;

        // 1. 状态打印
        if (ready && !ip_shown) {
            printf("udp_service: IP: %s\r\n", g_udp_net_ip);
            ip_shown = true;
        } else if (!ready) {
            ip_shown = false;
        }

        // 2. 定时任务 (广播、心跳、状态)
        if (ready) {
            uint64_t now = osal_get_jiffies();
            if (now - t_broadcast >= 2000) { // 2秒广播存在
                send_packet(0xFF, 0, 0, 0, 0, 0);
                t_broadcast = now;
            }
            if (now - t_heartbeat >= 10000) { // 10秒心跳
                send_packet(0xFE, 0, 0, 0, 0, 0);
                t_heartbeat = now;
            }
            check_and_send_state(now);
        }

        // 3. 接收处理 (非阻塞/超时模式)
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        ssize_t n = lwip_recvfrom(sockfd, g_buf, sizeof(g_buf), 0, (struct sockaddr *)&client_addr, &len);

        if (n > 0) {
            // 处理 OTA
            if (ota_service_process_packet(g_buf, (size_t)n, &client_addr))
                continue;

            // 处理控制协议 (长度7且校验通过)
            udp_packet_t *pkt = (udp_packet_t *)g_buf;
            if (n == 7 && udp_net_common_checksum8_add(g_buf, 6) == pkt->checksum) {
                switch (pkt->type) {
                    case 0x01: // 控制
                        udp_service_push_cmd(pkt->motor1, pkt->motor2, pkt->servo);
                        break;
                    case 0x03: // 模式切换
                        if (pkt->cmd <= 4) {
                            printf("udp_service: Set Mode %d\r\n", pkt->cmd);
                            robot_mgr_set_status((CarStatus)pkt->cmd);
                        }
                        break;
                    case 0x04: { // PID设置 (需要大括号才能声明变量)
                        int16_t val = (int16_t)(((uint8_t)pkt->motor1 << 8) | (uint8_t)pkt->motor2);
                        printf("udp_service: 设置pid值类型=%d 值=%d\r\n", pkt->cmd, val);
                        mode_trace_set_pid(pkt->cmd, val);
                        break;
                    }
                }
            }
        }

        osal_msleep(50);
    }
    return NULL;
}