/**
 * @file        udp_channel.c
 * @brief       UDP 控制通道实现
 * @details     实现广播发现与连接维持的双模状态机；收包只翻译为统一协议交 car_ctrl，
 *              不做任何电机/模式决策
 */

#include "udp_channel.h"

#include <stdio.h>
#include <string.h>

#include "../services/wifi_mgr_service.h"
#include "../core/car_ctrl.h"
#include "../core/car_state.h"
#include "../core/mode_trace.h"
#include "../car_common.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "lwip/inet.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"

#include "securec.h"
#include "soc_osal.h"
#include "../services/udp_net_common.h"

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

// WiFi 事件队列（wifi_mgr 任务 → udp 任务）：UDP 通道对 WiFi 状态的感知全部走这里，
// 不再轮询 g_wifi_status 全局变量
#define UDP_WIFI_EVT_QUEUE_DEPTH 4
static unsigned long g_wifi_evt_queue = 0;   // WiFi 事件队列 ID
static bool g_wifi_evt_queue_inited = false; // 事件队列是否已初始化
static bool g_wifi_ready = false;            // 本通道维护的 WiFi 就绪视图（仅 udp 任务读写）
static bool g_ready_via_ap = false;          // 就绪来源是否为 AP（AP_STOPPED 仅在 AP 就绪后算丢失）

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

// 中枢应答回调：ACK 从本通道发回给已绑定的控制器（"从哪来回哪去"）
static void udp_reply(void *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    udp_channel_send_data(data, len);
}

// 根据包类型分发处理：WiFi配置转交 wifi_mgr，其余投递统一命令总线
static void process_packet(uint8_t *data, size_t len, struct sockaddr_in *sender)
{
    if (len < 1)
        return;

    switch (data[0]) {
        // --- WiFi 配置（变长包）：配网是 wifi_mgr 的权责，本通道只识别转发 + 发应答 ---
        case CAR_PKT_WIFI_CONNECT:
        case CAR_PKT_WIFI_SET: {
            uint8_t ack = 0xFF;
            if (wifi_mgr_handle_config_packet(data, len, &ack) && ack != 0xFF) {
                uint8_t ack_pkt[3] = {data[0], ack, 0};
                udp_net_common_send_to_addr(g_sockfd, ack_pkt, sizeof(ack_pkt), sender);
            }
            break;
        }

        // --- 其余统一协议包：投递命令总线，由 car_ctrl 任务串行处理 ---
        default: {
            if (len > CAR_CMD_MAX_PAYLOAD) {
                printf("[UDP] 包过长丢弃: type=0x%02X len=%d\r\n", data[0], (int)len);
                return;
            }
            car_cmd_t cmd = {.source = MODE_SRC_UDP, .reply = udp_reply, .reply_ctx = NULL, .len = (uint16_t)len};
            if (memcpy_s(cmd.data, sizeof(cmd.data), data, len) != EOK) {
                return;
            }
            (void)car_ctrl_post_cmd(&cmd);
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
    car_state_get_copy(&st);
    car_packet_t pkt = {0};
    pkt.type = 0x02;
    pkt.cmd = st.mode;
    pkt.motor1 = (int8_t)(st.distance / 2);
    pkt.motor2 = 0;
    pkt.ir_data = (st.ir_left & 1) | ((st.ir_middle & 1) << 1) | ((st.ir_right & 1) << 2);
    udp_net_common_send_to_addr(g_sockfd, &pkt, sizeof(pkt), &g_server_addr);
}

// 应用一条 WiFi 事件到本通道的就绪视图（仅 udp 任务调用，单写者无竞态）
static void apply_wifi_event(bsp_wifi_event_t event)
{
    switch (event) {
        case BSP_WIFI_EVENT_STA_GOT_IP:
            g_wifi_ready = true;
            g_ready_via_ap = false;
            g_discovery_ready = false; // 网卡可能变了，发现包按新 MAC 重建
            break;
        case BSP_WIFI_EVENT_AP_READY:
            g_wifi_ready = true;
            g_ready_via_ap = true;
            g_discovery_ready = false;
            break;
        case BSP_WIFI_EVENT_STA_FAIL:
        case BSP_WIFI_EVENT_STA_STOPPED:
            g_wifi_ready = false;
            break;
        case BSP_WIFI_EVENT_AP_STOPPED:
            // STA 拿到 IP 后正常拆除 AP 也会发此事件，只有就绪来源是 AP 时才算丢失
            if (g_ready_via_ap)
                g_wifi_ready = false;
            break;
        default:
            break;
    }
}

// UDP通道主任务：WiFi 事件驱动 + 状态机驱动广播发现、心跳维持与接收处理
static int udp_channel_task(void *arg)
{
    (void)arg;

    uint64_t t_send_loop = 0;
    uint64_t t_keepalive_decay = 0;

    while (!g_udp_should_exit) {
        if (!g_wifi_ready) {
            // WiFi 未就绪：阻塞等待事件（低频率超时仅用于响应退出标志），不再 5ms 轮询全局变量
            uint32_t evt;
            unsigned int esz = sizeof(evt);
            if (osal_msg_queue_read_copy(g_wifi_evt_queue, &evt, &esz, 500) == OSAL_SUCCESS) {
                apply_wifi_event((bsp_wifi_event_t)evt);
                printf("[UDP] WiFi 事件: %d，就绪=%d\r\n", (int)evt, (int)g_wifi_ready);
            }
            continue;
        }

        // WiFi 已就绪：非阻塞排空事件，若刚丢失则清理 socket 回到等待态
        uint32_t evt;
        unsigned int esz = sizeof(evt);
        while (osal_msg_queue_read_copy(g_wifi_evt_queue, &evt, &esz, OSAL_MSGQ_NO_WAIT) == OSAL_SUCCESS) {
            apply_wifi_event((bsp_wifi_event_t)evt);
            esz = sizeof(evt);
        }
        if (!g_wifi_ready) {
            printf("[UDP] WiFi 丢失，关闭 socket 等待恢复\r\n");
            g_udp_state = UDP_STATE_WAIT_WIFI;
            if (g_sockfd >= 0) {
                lwip_close(g_sockfd);
                g_sockfd = -1;
            }
            continue;
        }

        // socket 尚未建立（刚就绪或刚重建失败）→ 创建并切入广播发现
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
        // 主动休眠以防在特殊网络状态下 CPU 100% 空转
        osal_msleep(5);
    }

    if (g_sockfd >= 0) {
        lwip_close(g_sockfd);
        g_sockfd = -1;
    }
    if (g_udp_exit_sem_inited)
        osal_sem_up(&g_udp_exit_sem);

    return 0;
}

// WiFi 事件订阅回调（运行在 wifi_mgr 任务上下文）：
// 只把事件投进本通道队列，不碰任何内部状态——状态切换全部回到 udp 任务上下文串行执行
static void on_wifi_state_change(bsp_wifi_event_t event, const char *ip)
{
    (void)ip;
    if (!g_wifi_evt_queue_inited)
        return;
    uint32_t evt = (uint32_t)event;
    (void)osal_msgq_overwrite(g_wifi_evt_queue, UDP_WIFI_EVT_QUEUE_DEPTH, &evt, sizeof(evt));
}

// 初始化UDP通道：创建事件队列、订阅 WiFi 事件 + 创建任务线程
void udp_channel_init(void)
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

    if (!g_wifi_evt_queue_inited) {
        if (osal_msg_queue_create("udp_wifi", UDP_WIFI_EVT_QUEUE_DEPTH, &g_wifi_evt_queue, 0, sizeof(uint32_t)) ==
            OSAL_SUCCESS) {
            g_wifi_evt_queue_inited = true;
        } else {
            printf("[UDP] WiFi 事件队列创建失败\r\n");
            return;
        }
    }

    (void)wifi_mgr_subscribe(on_wifi_state_change);

    // 订阅前 WiFi 可能已就绪（一次性查询，非轮询），补齐就绪视图避免死等第一个事件
    if (g_wifi_status != WIFI_MSG_START) {
        g_wifi_ready = true;
        g_ready_via_ap = (g_wifi_status == WIFI_MSG_AP_READY);
    }

    g_udp_task = car_task_create_locked("udp_task", (osal_kthread_handler)udp_channel_task, NULL, 8192, 24);
}

// 向绑定的主机发送 UDP 数据包
void udp_channel_send_data(const uint8_t *data, size_t len)
{
    if (g_sockfd >= 0 && g_udp_state == UDP_STATE_CONNECTED) {
        (void)udp_net_common_send_to_addr(g_sockfd, data, len, &g_server_addr);
    }
}

// 获取并向主机发送红外传感器原始电压和阈值
void udp_channel_send_trace_info(void)
{
    if (g_sockfd < 0 || g_udp_state != UDP_STATE_CONNECTED) {
        return;
    }

    CarState st;
    car_state_get_copy(&st);

    // 格式: [0x0A, RawL_Hi, RawL_Lo, RawM_Hi, RawM_Lo, RawR_Hi, RawR_Lo, ThL_Hi, ThL_Lo, ThM_Hi, ThM_Lo, ThR_Hi, ThR_Lo]
    uint8_t pkt[13];
    pkt[0] = CAR_PKT_TRACE_INFO;
    pkt[1] = (uint8_t)((st.adc_left >> 8) & 0xFF);
    pkt[2] = (uint8_t)(st.adc_left & 0xFF);
    pkt[3] = (uint8_t)((st.adc_middle >> 8) & 0xFF);
    pkt[4] = (uint8_t)(st.adc_middle & 0xFF);
    pkt[5] = (uint8_t)((st.adc_right >> 8) & 0xFF);
    pkt[6] = (uint8_t)(st.adc_right & 0xFF);

    pkt[7] = (uint8_t)((st.th_left >> 8) & 0xFF);
    pkt[8] = (uint8_t)(st.th_left & 0xFF);
    pkt[9] = (uint8_t)((st.th_middle >> 8) & 0xFF);
    pkt[10] = (uint8_t)(st.th_middle & 0xFF);
    pkt[11] = (uint8_t)((st.th_right >> 8) & 0xFF);
    pkt[12] = (uint8_t)(st.th_right & 0xFF);

    (void)udp_net_common_send_to_addr(g_sockfd, pkt, sizeof(pkt), &g_server_addr);
}
