/**
 * @file        udp_service.c
 * @brief       UDP 通信服务实现
 * @details     实现广播发现与连接维持的双模状态机
 */

#include "udp_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "wifi_mgr_service.h"
#include "../core/mode_trace.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../robot_common.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "ota_service.h"

#include "securec.h"
#include "soc_osal.h"
#include "../../../platform/storage_service.h"
#include "ui_service.h"
#include "udp_net_common.h"

// --- 配置常量 ---
#define BROADCAST_INTERVAL_MS 500 // 寻找期：高频广播，快速被发现
#define CONNECTED_HEART_MS 2000   // 连接期：低频心跳
#define TIMEOUT_LIMIT_MS 5000     // 增加容错到 5秒，防止网络抖动导致的误判
#define UDP_RECV_TIMEOUT_MS 10    // 接收阻塞时间 (短时间，保证循环响应)
#define KEEPALIVE_MAX_COUNT 3     // 容错计次：连续3次未收到心跳才判定断连

// --- 协议定义 ---
typedef struct {
    uint8_t type; // 0xFF
    uint8_t mac[6];
    char name[16];
} discovery_packet_t;

// WiFi 配置扩展包（变长，最大 70 字节）
typedef struct {
    uint8_t type;     // 0xE0~0xE2
    uint8_t ssid_len; // SSID 长度（0~32）
    uint8_t pwd_len;  // 密码长度（0~63）
    char payload[64]; // SSID + 密码连续存放
} wifi_config_pkt_t;

// --- 全局变量 ---
static int g_sockfd = -1;
static osal_task *g_udp_task = NULL;
static volatile bool g_udp_should_exit = false;
static osal_semaphore g_udp_exit_sem;
static bool g_udp_exit_sem_inited = false;

// --- 状态机 ---
typedef enum {
    UDP_STATE_WAIT_WIFI = 0, // 等待 WiFi 拿到 IP
    UDP_STATE_DISCOVERING,   // WiFi 就绪，500ms 广播自身等待控制器
    UDP_STATE_CONNECTED,     // 已绑定控制器，2s 心跳 + keepalive 衰减
} udp_state_t;
static udp_state_t g_udp_state = UDP_STATE_WAIT_WIFI;

// 连接状态管理
static struct sockaddr_in g_server_addr;                // 当前绑定的控制器地址
static uint64_t g_last_recv_time = 0;                   // 最后一次收到数据的时间
static uint8_t g_keepalive_count = KEEPALIVE_MAX_COUNT; // 容错计次（生命值）

// 发现包管理
static discovery_packet_t g_discovery_pkt;
static bool g_discovery_ready = false; // 发现包是否已构建(MAC是否获取)

// --- 内部函数声明 ---
static int udp_service_task(void *arg);
static void handle_udp_receive(void);
static void process_packet(uint8_t *data, size_t len, struct sockaddr_in *sender);
static void build_discovery_packet(void);

// --------------------------------------------------------------------------
// 外部接口实现
// --------------------------------------------------------------------------

void udp_service_init(void)
{
    if (g_udp_task != NULL)
        return;
    g_udp_should_exit = false;
    if (!g_udp_exit_sem_inited) {
        osal_sem_binary_sem_init(&g_udp_exit_sem, 0);
        g_udp_exit_sem_inited = true;
    }
    while (osal_sem_trydown(&g_udp_exit_sem) == OSAL_SUCCESS) { }

    // 创建线程
    g_udp_task = robot_task_create_locked("udp_task", (osal_kthread_handler)udp_service_task, NULL, UDP_STACK_SIZE,
                                          UDP_TASK_PRIORITY);
}

WifiConnectStatus udp_service_get_wifi_status(void)
{
    bsp_wifi_mode_t mode = bsp_wifi_get_mode();
    bsp_wifi_status_t status = bsp_wifi_get_status();

    if (mode == BSP_WIFI_MODE_AP)
        return WIFI_STATUS_AP_MODE;
    if (status == BSP_WIFI_STATUS_GOT_IP)
        return WIFI_STATUS_CONNECTED;
    if (status == BSP_WIFI_STATUS_CONNECTING || status == BSP_WIFI_STATUS_CONNECTED)
        return WIFI_STATUS_CONNECTING;
    return WIFI_STATUS_DISCONNECTED;
}

const char *udp_service_get_ip(void)
{
    static char ip_buf[BUF_IP] = "0.0.0.0";
    bsp_wifi_get_ip(ip_buf, sizeof(ip_buf));
    return ip_buf;
}

// --------------------------------------------------------------------------
// 内部逻辑实现
// --------------------------------------------------------------------------

/**
 * @brief 在WiFi连接成功后，延迟构建发现包(确保获取到MAC)
 */
static void build_discovery_packet(void)
{
    if (g_discovery_ready)
        return;

    memset_s(&g_discovery_pkt, sizeof(g_discovery_pkt), 0, sizeof(g_discovery_pkt));
    g_discovery_pkt.type = ROBOT_PKT_DISCOVERY;

    // 尝试获取MAC地址
    if (udp_net_get_mac_address(g_discovery_pkt.mac) == 0) {
        // MAC获取成功，生成设备名
        snprintf(g_discovery_pkt.name, sizeof(g_discovery_pkt.name), "Robot_%02X%02X", g_discovery_pkt.mac[4],
                 g_discovery_pkt.mac[5]);

        g_discovery_ready = true;
        printf("[UDP] 发现包构建完成 (MAC: %02X:%02X...)\r\n", g_discovery_pkt.mac[0], g_discovery_pkt.mac[1]);
    }
}

/**
 * @brief 处理具体的业务包逻辑
 */
static void send_wifi_config_ack(uint8_t type, uint8_t status, struct sockaddr_in *sender)
{
    uint8_t ack[3] = {type, status, 0};
    udp_net_common_send_to_addr(g_sockfd, ack, sizeof(ack), sender);
}

static void wifi_config_extract_creds(const wifi_config_pkt_t *pkt, char *ssid, size_t ssid_sz,
                                      char *pwd, size_t pwd_sz)
{
    if (pkt->ssid_len > 0 && pkt->ssid_len < ssid_sz) {
        (void)memcpy_s(ssid, ssid_sz, pkt->payload, pkt->ssid_len);
        ssid[pkt->ssid_len] = '\0';
    }
    if (pkt->pwd_len > 0 && pkt->pwd_len < pwd_sz) {
        (void)memcpy_s(pwd, pwd_sz, pkt->payload + pkt->ssid_len, pkt->pwd_len);
        pwd[pkt->pwd_len] = '\0';
    }
}

static void handle_wifi_config(uint8_t *data, size_t len, struct sockaddr_in *sender)
{
    if (len < 3)
        return;
    wifi_config_pkt_t *pkt = (wifi_config_pkt_t *)data;

    switch (pkt->type) {
        case UDP_CMD_WIFI_CONFIG_SET: {
            char ssid[33] = {0};
            char pwd[64] = {0};
            wifi_config_extract_creds(pkt, ssid, sizeof(ssid), pwd, sizeof(pwd));
            errcode_t ret = storage_service_save_wifi_config(ssid, pwd);
            send_wifi_config_ack(pkt->type, (ret == ERRCODE_SUCC) ? 0x00 : 0x01, sender);
            printf("[UDP] WiFi配置保存: SSID='%s' 结果=%d\r\n", ssid, ret);
            break;
        }

        case UDP_CMD_WIFI_CONFIG_CONNECT: {
            char ssid[33] = {0};
            char pwd[64] = {0};
            wifi_config_extract_creds(pkt, ssid, sizeof(ssid), pwd, sizeof(pwd));
            bsp_wifi_connect_ap(ssid, pwd);
            printf("[UDP] WiFi切换STA: SSID='%s'\r\n", ssid);
            break;
        }

        case UDP_CMD_WIFI_CONFIG_GET: {
            char ssid[32] = {0};
            char pwd[64] = {0};
            storage_service_get_wifi_config(ssid, pwd);
            uint8_t ack[70] = {0};
            ack[0] = pkt->type;
            ack[1] = 0x00; // 成功
            size_t ssid_len = strlen(ssid);
            size_t pwd_len = strlen(pwd);
            if (ssid_len > 32)
                ssid_len = 32;
            if (pwd_len > 63)
                pwd_len = 63;
            ack[2] = (uint8_t)ssid_len;
            ack[3] = (uint8_t)pwd_len;
            if (ssid_len > 0) {
                (void)memcpy_s(ack + 4, sizeof(ack) - 4, ssid, ssid_len);
            }
            if (pwd_len > 0) {
                (void)memcpy_s(ack + 4 + ssid_len, sizeof(ack) - 4 - ssid_len, pwd, pwd_len);
            }
            udp_net_common_send_to_addr(g_sockfd, ack, 4 + ssid_len + pwd_len, sender);
            break;
        }
    }
}

static void process_packet(uint8_t *data, size_t len, struct sockaddr_in *sender)
{
    if (len < 1)
        return;
    uint8_t type = data[0];

    // WiFi配置命令
    if (type >= 0xE0 && type <= 0xE2) {
        handle_wifi_config(data, len, sender);
        return;
    }

    // 标准5字节协议包 → 先走统一处理
    if (len == sizeof(robot_packet_t)) {
        robot_packet_t *pkt = (robot_packet_t *)data;

        if (robot_proto_handle_packet(pkt, MODE_SRC_UDP))
            return; // CONTROL / MODE / HEARTBEAT 已由统一处理器消费

        // PID 设参（仅 UDP 支持）
        if (pkt->type == ROBOT_PKT_PID) {
            if (pkt->cmd == PID_PARAM_SAVE) {
                mode_trace_save_pid();
            } else {
                mode_trace_set_pid(pkt->cmd, (int16_t)((pkt->motor1 << 8) | (uint8_t)pkt->motor2));
            }
            return;
        }

        // OTA 触发
        if (pkt->type == ROBOT_PKT_OTA) {
            robot_packet_t ack = {0};
            ack.type = ROBOT_PKT_OTA;
            if (pkt->cmd == OTA_SUBCMD_START) {
                if (ota_service_start(0)) {
                    ack.cmd = 0x00;
                } else {
                    ack.cmd = 0x01;
                }
            } else if (pkt->cmd == 0x02) {
                ota_service_cancel();
                ack.cmd = 0x00;
            } else {
                ack.cmd = 0x02;
            }
            udp_net_common_send_to_addr(g_sockfd, &ack, sizeof(ack), &g_server_addr);
            return;
        }
    }
}

/**
 * @brief 接收处理
 */
static void handle_udp_receive(void)
{
    uint8_t buf[128];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // 非阻塞接收
    int n = lwip_recvfrom(g_sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &addr_len);

    if (n > 0) {
        uint64_t now = osal_get_jiffies();

        // --- 单向触发重连：只要收到服务端任何指令包，立即进入连接态 ---
        if (g_udp_state != UDP_STATE_CONNECTED || client_addr.sin_addr.s_addr != g_server_addr.sin_addr.s_addr ||
            client_addr.sin_port != g_server_addr.sin_port) {
            g_udp_state = UDP_STATE_CONNECTED;
            memcpy_s(&g_server_addr, sizeof(g_server_addr), &client_addr, sizeof(client_addr));
            printf("[UDP] 建立连接/更新地址: %s\r\n", inet_ntoa(client_addr.sin_addr));
        }

        // 重置容错计次（生命值回满）
        g_keepalive_count = KEEPALIVE_MAX_COUNT;
        g_last_recv_time = now;

        // 处理数据包
        process_packet(buf, (size_t)n, &client_addr);
    }
}

/**
 * @brief 发送机器人状态 (同时作为心跳包)
 */
static void send_heartbeat(void)
{
    RobotState curr;
    robot_mgr_get_state_copy(&curr);
    robot_packet_t pkt = {0};
    pkt.type = 0x02;
    pkt.cmd = curr.mode;
    pkt.motor1 = (int8_t)(curr.distance / 2);
    pkt.motor2 = 0;
    pkt.ir_data = (curr.ir_left & 1) | ((curr.ir_middle & 1) << 1) | ((curr.ir_right & 1) << 2);
    udp_net_common_send_to_addr(g_sockfd, &pkt, sizeof(pkt), &g_server_addr);
}

/**
 * @brief UDP 服务主任务
 */
static int udp_service_task(void *arg)
{
    (void)arg;

    // 打开Socket，设置接收超时为 10ms
    g_sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, UDP_RECV_TIMEOUT_MS, true);
    if (g_sockfd < 0) {
        printf("[UDP] Socket 创建失败\r\n");
        return 0;
    }

    uint64_t t_send_loop = 0;
    uint64_t t_keepalive_decay = 0;
    bool wifi_was_ready = false;
    static bsp_wifi_mode_t last_mode = BSP_WIFI_MODE_STA;
    static char last_ip[BUF_IP] = {0};

    while (!g_udp_should_exit) {
        if (g_sockfd < 0) {
            g_sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, UDP_RECV_TIMEOUT_MS, true);
            if (g_sockfd < 0)
                continue;
        }

        uint64_t now = osal_get_jiffies();

        // --- WiFi 状态 + 模式变化检测（状态机无关的前置处理） ---
        bsp_wifi_mode_t curr_mode = bsp_wifi_get_mode();
        bsp_wifi_status_t wifi_status = bsp_wifi_get_status();

        bool wifi_connected, wifi_has_ip;
        if (curr_mode == BSP_WIFI_MODE_AP) {
            wifi_connected = (wifi_status == BSP_WIFI_STATUS_GOT_IP);
            wifi_has_ip = wifi_connected;
        } else {
            wifi_connected = (wifi_status == BSP_WIFI_STATUS_GOT_IP || wifi_status == BSP_WIFI_STATUS_CONNECTED);
            wifi_has_ip = (wifi_status == BSP_WIFI_STATUS_GOT_IP);
        }
        char curr_ip[BUF_IP] = "0.0.0.0";
        if (wifi_has_ip) {
            bsp_wifi_get_ip(curr_ip, sizeof(curr_ip));
        }
        bool wifi_ready = wifi_connected && wifi_has_ip;

        if (curr_mode != last_mode) {
            wifi_was_ready = false;
            last_ip[0] = '\0';
            g_discovery_ready = false;
            if (g_sockfd >= 0) {
                lwip_close(g_sockfd);
                g_sockfd = -1;
            }
            printf("[UDP] WiFi 模式变化: %d -> %d, 重建 socket\r\n", (int)last_mode, (int)curr_mode);
        }

        bool ip_changed = (wifi_ready && strcmp(last_ip, curr_ip) != 0);
        if ((wifi_ready && !wifi_was_ready) || ip_changed) {
            ui_show_mode_page(CAR_STOP_STATUS);
            wifi_was_ready = true;
            (void)strncpy_s(last_ip, sizeof(last_ip), curr_ip, sizeof(last_ip) - 1);
        } else if (!wifi_ready) {
            wifi_was_ready = false;
        }
        last_mode = curr_mode;

        // --- 状态机 ---
        switch (g_udp_state) {
            case UDP_STATE_WAIT_WIFI:
                if (wifi_ready) {
                    g_udp_state = UDP_STATE_DISCOVERING;
                    g_keepalive_count = KEEPALIVE_MAX_COUNT;
                }
                break;

            case UDP_STATE_DISCOVERING:
                if (!wifi_ready) {
                    g_udp_state = UDP_STATE_WAIT_WIFI;
                    g_discovery_ready = false;
                    g_keepalive_count = KEEPALIVE_MAX_COUNT;
                    break;
                }
                if (!g_discovery_ready)
                    build_discovery_packet();
                if (now - t_send_loop >= osal_msecs_to_jiffies(BROADCAST_INTERVAL_MS)) {
                    t_send_loop = now;
                    if (g_discovery_ready)
                        udp_net_common_send_broadcast(g_sockfd, &g_discovery_pkt, sizeof(g_discovery_pkt), UDP_BROADCAST_PORT);
                }
                break;

            case UDP_STATE_CONNECTED:
                if (!wifi_ready) {
                    g_udp_state = UDP_STATE_WAIT_WIFI;
                    g_discovery_ready = false;
                    g_keepalive_count = KEEPALIVE_MAX_COUNT;
                    break;
                }
                if (now - t_keepalive_decay >= osal_msecs_to_jiffies(1000)) {
                    t_keepalive_decay = now;
                    if (g_keepalive_count > 0) {
                        if (--g_keepalive_count == 0) {
                            printf("[UDP] 连接超时，切回广播模式\r\n");
                            g_udp_state = UDP_STATE_DISCOVERING;
                            memset_s(&g_server_addr, sizeof(g_server_addr), 0, sizeof(g_server_addr));
                        }
                    }
                }
                if (now - t_send_loop >= osal_msecs_to_jiffies(CONNECTED_HEART_MS)) {
                    t_send_loop = now;
                    send_heartbeat();
                }
                break;
        }

        // 接收始终执行；handle_udp_receive 任一包到达即触发 DISCOVERING→CONNECTED
        handle_udp_receive();
    }

    if (g_sockfd >= 0) {
        lwip_close(g_sockfd);
        g_sockfd = -1;
    }
    if (g_udp_exit_sem_inited) {
        osal_sem_up(&g_udp_exit_sem);
    }
    return 0;
}