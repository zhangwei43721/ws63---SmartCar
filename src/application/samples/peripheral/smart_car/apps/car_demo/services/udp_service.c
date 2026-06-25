/**
 * @file        udp_service.c
 * @brief       UDP 通信服务实现
 * @details     实现广播发现与连接维持的双模状态机
 */

#include "udp_service.h"

#include <stdio.h>
#include <string.h>

#include "wifi_mgr_service.h"
#include "../core/mode_trace.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "lwip/inet.h"
#include "lwip/netifapi.h"
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
    uint8_t type;     // 0xE0~0xE1
    uint8_t ssid_len; // SSID 长度（0~32）
    uint8_t pwd_len;  // 密码长度（0~63）
    char payload[64]; // SSID + 密码连续存放
} wifi_config_pkt_t;

// --- 全局变量 ---
static int g_sockfd = -1;                       // UDP 服务 socket 文件描述符
static osal_task *g_udp_task = NULL;            // UDP 服务任务句柄
static volatile bool g_udp_should_exit = false; // UDP 任务退出标志
static osal_semaphore g_udp_exit_sem;           // UDP 任务退出同步信号量
static bool g_udp_exit_sem_inited = false;      // 退出信号量是否已初始化

// --- 状态机 ---
typedef enum {
    UDP_STATE_WAIT_WIFI = 0, // 等待 WiFi 拿到 IP
    UDP_STATE_DISCOVERING,   // WiFi 就绪，500ms 广播自身等待控制器
    UDP_STATE_CONNECTED,     // 已绑定控制器，2s 心跳 + keepalive 衰减
} udp_state_t;

static udp_state_t g_udp_state = UDP_STATE_WAIT_WIFI;  // 

// 连接状态管理
static struct sockaddr_in g_server_addr;                // 当前绑定的控制器地址
static uint64_t g_last_recv_time = 0;                   // 最后一次收到数据的时间
static uint8_t g_keepalive_count = KEEPALIVE_MAX_COUNT; // 容错计次（生命值）

// 发现包管理
static discovery_packet_t g_discovery_pkt;
static bool g_discovery_ready = false; // 发现包是否已构建(MAC是否获取)

// 延迟构建UDP广播发现包（等待MAC地址就绪）
static void build_discovery_packet(void)
{
    if (g_discovery_ready)
        return;

    memset_s(&g_discovery_pkt, sizeof(g_discovery_pkt), 0, sizeof(g_discovery_pkt));
    g_discovery_pkt.type = CAR_PKT_DISCOVERY;

    // 尝试获取MAC地址
    struct netif *netif_p = netifapi_netif_find("wlan0"); // 找sta网卡
    if (netif_p == NULL)
        netif_p = netifapi_netif_find("ap0"); // 找ap网卡
    if (netif_p) {
        memcpy_s(g_discovery_pkt.mac, 6, netif_p->hwaddr, 6);
        // MAC获取成功，生成设备名
        snprintf(g_discovery_pkt.name, sizeof(g_discovery_pkt.name), "Car_%02X%02X", g_discovery_pkt.mac[4],
                 g_discovery_pkt.mac[5]);

        g_discovery_ready = true;
        printf("[UDP] 发现包构建完成 (MAC: %02X:%02X...)\r\n", g_discovery_pkt.mac[0], g_discovery_pkt.mac[1]);
    }
}

// 从WiFi配置包中提取SSID和密码
static void wifi_config_extract_creds(const wifi_config_pkt_t *pkt,
                                      char *ssid,
                                      size_t ssid_sz,
                                      char *pwd,
                                      size_t pwd_sz)
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

// 根据包类型分发处理：WiFi配置、统一协议、PID调参、OTA触发
static void process_packet(uint8_t *data, size_t len, struct sockaddr_in *sender)
{
    if (len < 1)
        return;

    switch (data[0]) {
        // --- WiFi 配置（变长包）---
        case CAR_PKT_WIFI_CONNECT:
        case CAR_PKT_WIFI_SET: {
            // 公共校验 + 提取凭证
            if (len < 3)
                return;
            wifi_config_pkt_t *pkt = (wifi_config_pkt_t *)data;
            if (pkt->ssid_len > 32 || pkt->pwd_len > 63 || (pkt->ssid_len + pkt->pwd_len) > 64)
                return;
            if (len < (size_t)(3 + pkt->ssid_len + pkt->pwd_len))
                return;
            char ssid[33] = {0};
            char pwd[64] = {0};
            wifi_config_extract_creds(pkt, ssid, sizeof(ssid), pwd, sizeof(pwd));

            switch (data[0]) {
                case CAR_PKT_WIFI_CONNECT: // 0xE1 立即连接
                    bsp_wifi_connect_ap(ssid, pwd);
                    printf("[UDP] WiFi切换STA: SSID='%s'\r\n", ssid);
                    break;
                case CAR_PKT_WIFI_SET: { // 0xE0 保存到 NV 并回 ACK
                    errcode_t ret = storage_service_save_wifi_config(ssid, pwd);
                    uint8_t ack[3] = {CAR_PKT_WIFI_SET, (ret == ERRCODE_SUCC) ? 0x00 : 0x01, 0};
                    udp_net_common_send_to_addr(g_sockfd, ack, sizeof(ack), sender);
                    printf("[UDP] WiFi配置保存: SSID='%s' 结果=%d\r\n", ssid, ret);
                    break;
                }
            }
            break;
        }

        // --- 标准 5 字节协议包 ---
        case CAR_PKT_CONTROL:   // 0x01 电机控制
        case CAR_PKT_MODE:      // 0x03 模式切换
        case CAR_PKT_HEARTBEAT: // 0xFE 心跳
            if (len == sizeof(car_packet_t))
                car_proto_handle_packet((car_packet_t *)data, MODE_SRC_UDP);
            break;

        case CAR_PKT_PID: { // 0x04 PID 调参（退出循迹时自动持久化到 NV）
            if (len != sizeof(car_packet_t))
                return;
            car_packet_t *pkt = (car_packet_t *)data;
            mode_trace_set_pid(pkt->cmd, (int16_t)((pkt->motor1 << 8) | (uint8_t)pkt->motor2));
            break;
        }

        case CAR_PKT_OTA: { // 0x05 OTA 触发/取消
            if (len != sizeof(car_packet_t))
                return;
            car_packet_t *pkt = (car_packet_t *)data;
            car_packet_t ack = {0};
            ack.type = CAR_PKT_OTA;
            if (pkt->cmd == OTA_SUBCMD_START) {
                ack.cmd = ota_service_start(0) ? 0x00 : 0x01;
            } else if (pkt->cmd == 0x02) {
                ota_service_cancel();
                ack.cmd = 0x00;
            } else {
                ack.cmd = 0x02;
            }
            udp_net_common_send_to_addr(g_sockfd, &ack, sizeof(ack), &g_server_addr);
            break;
        }
    }
}

// 非阻塞接收UDP数据包，更新连接状态并分发处理
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

// 发送机器人状态心跳包到已连接的控制器
static void send_heartbeat(void)
{
    CarState st;
    car_mgr_get_state_copy(&st);
    car_packet_t pkt = {0};
    pkt.type = 0x02;
    pkt.cmd = st.mode;
    pkt.motor1 = (int8_t)(st.distance / 2);
    pkt.motor2 = 0;
    pkt.ir_data = (st.ir_left & 1) | ((st.ir_middle & 1) << 1) | ((st.ir_right & 1) << 2);
    udp_net_common_send_to_addr(g_sockfd, &pkt, sizeof(pkt), &g_server_addr);
}

// UDP服务主任务：状态机驱动广播发现、心跳维持与接收处理
static int udp_service_task(void *arg)
{
    (void)arg;

    // 初始 socket 创建
    g_sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, UDP_RECV_TIMEOUT_MS, true);
    if (g_sockfd < 0) {
        printf("[UDP] Socket 创建失败\r\n");
        return 0;
    }

    uint64_t t_send_loop = 0;
    uint64_t t_keepalive_decay = 0;

    while (!g_udp_should_exit) {
        // WiFi 未就绪时阻塞等待，清理旧 Socket 并切到 WAIT_WIFI
        if (g_wifi_status == WIFI_MSG_START) {
            g_udp_state = UDP_STATE_WAIT_WIFI;
            if (g_sockfd >= 0) {
                lwip_close(g_sockfd);
                g_sockfd = -1;
            }
            osal_msleep(50);
            continue;
        }

        // socket 被回调关闭后重建
        if (g_sockfd < 0) {
            g_sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, UDP_RECV_TIMEOUT_MS, true);
            if (g_sockfd < 0) {
                osal_msleep(50);
                continue;
            }
            g_udp_state = UDP_STATE_DISCOVERING;
            g_keepalive_count = KEEPALIVE_MAX_COUNT;
        }

        uint64_t now = osal_get_jiffies();

        // --- 状态机（WiFi 此时必定就绪） ---
        switch (g_udp_state) {
            case UDP_STATE_WAIT_WIFI:
                // WiFi 就绪 + socket 存在 → 直接切入广播
                if (g_sockfd >= 0) {
                    g_udp_state = UDP_STATE_DISCOVERING;
                    g_keepalive_count = KEEPALIVE_MAX_COUNT;
                }
                break;

            case UDP_STATE_DISCOVERING:
                // 广播发现态：构建发现包，每 500ms 广播一次，等待控制器主动发包触发连接
                if (!g_discovery_ready)
                    build_discovery_packet();
                if (now - t_send_loop >= osal_msecs_to_jiffies(BROADCAST_INTERVAL_MS)) {
                    t_send_loop = now;
                    if (g_discovery_ready) {
                        struct sockaddr_in bcast_addr = {0};
                        bcast_addr.sin_family = AF_INET;
                        bcast_addr.sin_port = lwip_htons(UDP_BROADCAST_PORT);
                        bcast_addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);
                        lwip_sendto(g_sockfd, &g_discovery_pkt, sizeof(g_discovery_pkt), 0,
                                    (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
                    }
                }
                break;

            case UDP_STATE_CONNECTED:
                // 已连接态：每秒衰减 keepalive 计数，计数归零则判定断连切回广播；同时每 2s 发送心跳
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

        handle_udp_receive();
    }

    if (g_sockfd >= 0) {
        lwip_close(g_sockfd);
        g_sockfd = -1;
    }
    if (g_udp_exit_sem_inited)
        osal_sem_up(&g_udp_exit_sem);

    return 0;
}

// WiFi 状态变化回调（由 wifi_mgr_service 通过函数指针 s_state_cb 调用）
// 职责：只清发现包标记，让主循环下次重建。不关 socket，不改 g_udp_state，
//       所有状态切换由 udp_service_task 主循环根据 g_wifi_status == WIFI_MSG_START 统一驱动。
static void on_wifi_state_change(bsp_wifi_event_t event, const char *ip)
{
    (void)ip;
    printf("[UDP] WiFi 状态变化: event=%d\r\n", (int)event);
    g_discovery_ready = false;
}

// 初始化UDP服务：注册 WiFi 状态回调 + 创建任务线程
void udp_service_init(void)
{
    if (g_udp_task != NULL)
        return;
    g_udp_should_exit = false;
    if (!g_udp_exit_sem_inited) {
        osal_sem_binary_sem_init(&g_udp_exit_sem, 0);
        g_udp_exit_sem_inited = true;
    }
    while (osal_sem_trydown(&g_udp_exit_sem) == OSAL_SUCCESS) {
    }

    // 把 on_wifi_state_change 函数地址注册到 wifi_mgr_service 的 s_state_cb 指针
    // 之后 wifi_mgr 每次 WiFi 状态变化都会通过 s_state_cb(event, ip) 回调到这里
    bsp_wifi_mgr_register_cb(on_wifi_state_change);
    g_udp_task = car_task_create_locked("udp_task", (osal_kthread_handler)udp_service_task, NULL, 8192, 24);
}