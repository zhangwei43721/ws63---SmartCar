/**
 * @file        udp_service.c
 * @brief       UDP 通信服务实现 (优化版)
 * @details     实现广播发现与连接维持的双模状态机
 */

#include "udp_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "../core/mode_trace.h"
#include "../core/motor_executor.h"
#include "../robot_common.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "ota_service.h"
#include "securec.h"
#include "soc_osal.h"
#include "storage_service.h"
#include "ui_service.h"
#include "udp_net_common.h"

/* --- 配置常量 --- */
#define BROADCAST_INTERVAL_MS 500  // 寻找期：高频广播，快速被发现
#define CONNECTED_HEART_MS 2000    // 连接期：低频心跳
#define TIMEOUT_LIMIT_MS 5000      // 增加容错到 5秒，防止网络抖动导致的误判
#define UDP_RECV_TIMEOUT_MS 10     // 接收阻塞时间 (短时间，保证循环响应)
#define KEEPALIVE_MAX_COUNT 3      // 容错计次：连续3次未收到心跳才判定断连

/* --- 协议定义 --- */
#pragma pack(1)
typedef struct {
  uint8_t type;  // 01=控制, 02=状态, 03=模式, 04=PID, FE=心跳, FF=广播
  uint8_t cmd;
  int8_t motor1;
  int8_t motor2;
  int8_t ir_data;
} udp_packet_t;

typedef struct {
  uint8_t type;  // 0xFF
  uint8_t mac[6];
  char name[16];
} discovery_packet_t;

/* WiFi 配置扩展包（变长，最大 70 字节） */
typedef struct {
  uint8_t  type;       // 0xE0~0xE2
  uint8_t  ssid_len;   // SSID 长度（0~32）
  uint8_t  pwd_len;    // 密码长度（0~63）
  char     payload[64];// SSID + 密码连续存放
} wifi_config_pkt_t;
#pragma pack()

/* --- 全局变量 --- */
static int g_sockfd = -1;
static osal_task *g_udp_task = NULL;
static volatile bool g_udp_should_exit = false;
static osal_mutex g_cmd_mutex;

// 命令缓存
static struct {
  int8_t m1, m2;
  bool new_data;
} g_cmd_cache = {0};

// 连接状态管理
static struct sockaddr_in g_server_addr;  // 当前绑定的控制器地址
static bool g_is_connected = false;       // 是否处于已连接状态
static uint64_t g_last_recv_time = 0;     // 最后一次收到数据的时间
static uint8_t g_keepalive_count = KEEPALIVE_MAX_COUNT;  // 容错计次（生命值）

// 发现包管理
static discovery_packet_t g_discovery_pkt;
static bool g_discovery_ready = false;  // 发现包是否已构建(MAC是否获取)

/* --- 内部函数声明 --- */
static int udp_service_task(void* arg);
static void handle_udp_receive(void);
static void process_packet(uint8_t* data, size_t len,
                           struct sockaddr_in* sender);
static void build_discovery_packet(void);

/* -------------------------------------------------------------------------- */
/* 外部接口实现                                      */
/* -------------------------------------------------------------------------- */

void udp_service_init(void) {
  udp_net_common_init();
  osal_mutex_init(&g_cmd_mutex);

  if (g_udp_task != NULL) return;
  g_udp_should_exit = false;

  // 创建线程
  osal_kthread_lock();
  g_udp_task = osal_kthread_create((osal_kthread_handler)udp_service_task,
                                   NULL, "udp_task", UDP_STACK_SIZE);
  if (g_udp_task != NULL) osal_kthread_set_priority(g_udp_task, UDP_TASK_PRIORITY);
  osal_kthread_unlock();
}

void udp_service_deinit(void) {
  g_udp_should_exit = true;
  if (g_sockfd >= 0) {
    lwip_close(g_sockfd);
    g_sockfd = -1;
  }
}

WifiConnectStatus udp_service_get_wifi_status(void) {
  if (bsp_wifi_get_mode() == BSP_WIFI_MODE_AP) return WIFI_STATUS_AP_MODE;
  if (g_udp_net_wifi_connected && g_udp_net_wifi_has_ip)
    return WIFI_STATUS_CONNECTED;
  return g_udp_net_wifi_connected ? WIFI_STATUS_CONNECTING
                                  : WIFI_STATUS_DISCONNECTED;
}

const char* udp_service_get_ip(void) { return g_udp_net_ip; }

void udp_service_push_cmd(int8_t m1, int8_t m2) {
  motor_executor_push_cmd(m1, m2);
}

bool udp_service_pop_cmd(int8_t* m1, int8_t* m2) {
  bool ret = false;
  osal_mutex_lock(&g_cmd_mutex);
  if (g_cmd_cache.new_data) {
    *m1 = g_cmd_cache.m1;
    *m2 = g_cmd_cache.m2;
    g_cmd_cache.new_data = false;
    ret = true;
  }
  osal_mutex_unlock(&g_cmd_mutex);
  return ret;
}

/* -------------------------------------------------------------------------- */
/* 内部逻辑实现                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief 在WiFi连接成功后，延迟构建发现包(确保获取到MAC)
 */
static void build_discovery_packet(void) {
  if (g_discovery_ready) return;

  memset_s(&g_discovery_pkt, sizeof(g_discovery_pkt), 0,
           sizeof(g_discovery_pkt));
  g_discovery_pkt.type = 0xFF;

  // 尝试获取MAC地址
  if (udp_net_get_mac_address(g_discovery_pkt.mac) == 0) {
    // MAC获取成功，生成设备名
    snprintf(g_discovery_pkt.name, sizeof(g_discovery_pkt.name),
             "Robot_%02X%02X", g_discovery_pkt.mac[4], g_discovery_pkt.mac[5]);

    g_discovery_ready = true;
    printf("[UDP] 发现包构建完成 (MAC: %02X:%02X...)\r\n",
           g_discovery_pkt.mac[0], g_discovery_pkt.mac[1]);
  }
}

/**
 * @brief 处理具体的业务包逻辑
 */
static void send_wifi_config_ack(uint8_t type, uint8_t status,
                                  struct sockaddr_in* sender) {
  uint8_t ack[3] = {type, status, 0};
  udp_net_common_send_to_addr(ack, sizeof(ack), sender);
}

static void handle_wifi_config(uint8_t* data, size_t len,
                                struct sockaddr_in* sender) {
  if (len < 3) return;
  wifi_config_pkt_t* pkt = (wifi_config_pkt_t*)data;

  switch (pkt->type) {
    case UDP_CMD_WIFI_CONFIG_SET: {
      char ssid[33] = {0};
      char pwd[64] = {0};
      if (pkt->ssid_len > 0 && pkt->ssid_len <= 32) {
        (void)memcpy_s(ssid, sizeof(ssid), pkt->payload, pkt->ssid_len);
        ssid[pkt->ssid_len] = '\0';
      }
      if (pkt->pwd_len > 0 && pkt->pwd_len <= 63) {
        (void)memcpy_s(pwd, sizeof(pwd), pkt->payload + pkt->ssid_len, pkt->pwd_len);
        pwd[pkt->pwd_len] = '\0';
      }
      errcode_t ret = storage_service_save_wifi_config(ssid, pwd);
      send_wifi_config_ack(pkt->type,
                           (ret == ERRCODE_SUCC) ? 0x00 : 0x01, sender);
      printf("[UDP] WiFi配置保存: SSID='%s' 结果=%d\r\n", ssid, ret);
      break;
    }

    case UDP_CMD_WIFI_CONFIG_CONNECT: {
      char ssid[32] = {0};
      char pwd[64] = {0};
      if (pkt->ssid_len > 0 && pkt->ssid_len <= 32) {
        (void)memcpy_s(ssid, sizeof(ssid), pkt->payload, pkt->ssid_len);
        ssid[pkt->ssid_len] = '\0';
      }
      if (pkt->pwd_len > 0 && pkt->pwd_len <= 63) {
        (void)memcpy_s(pwd, sizeof(pwd), pkt->payload + pkt->ssid_len, pkt->pwd_len);
        pwd[pkt->pwd_len] = '\0';
      }
      /* 先保存 */
      (void)storage_service_save_wifi_config(ssid, pwd);
      /* 再切换 */
      int ret = bsp_wifi_switch_from_ap_to_sta(ssid, pwd);
      send_wifi_config_ack(pkt->type,
                           (ret == 0) ? 0x00 : 0x01, sender);
      printf("[UDP] WiFi切换STA: SSID='%s' 结果=%d\r\n", ssid, ret);
      break;
    }

    case UDP_CMD_WIFI_CONFIG_GET: {
      char ssid[32] = {0};
      char pwd[64] = {0};
      storage_service_get_wifi_config(ssid, pwd);
      uint8_t ack[70] = {0};
      ack[0] = pkt->type;
      ack[1] = 0x00;  // success
      size_t ssid_len = strlen(ssid);
      size_t pwd_len = strlen(pwd);
      if (ssid_len > 32) ssid_len = 32;
      if (pwd_len > 63) pwd_len = 63;
      ack[2] = (uint8_t)ssid_len;
      ack[3] = (uint8_t)pwd_len;
      if (ssid_len > 0) {
        (void)memcpy_s(ack + 4, sizeof(ack) - 4, ssid, ssid_len);
      }
      if (pwd_len > 0) {
        (void)memcpy_s(ack + 4 + ssid_len, sizeof(ack) - 4 - ssid_len, pwd, pwd_len);
      }
      udp_net_common_send_to_addr(ack, 4 + ssid_len + pwd_len, sender);
      break;
    }
  }
}

static void process_packet(uint8_t* data, size_t len,
                           struct sockaddr_in* sender) {
  if (len < 1) return;
  uint8_t type = data[0];

  // WiFi配置命令
  if (type >= 0xE0 && type <= 0xE2) {
    handle_wifi_config(data, len, sender);
    return;
  }

  // 标准5字节包
  if (len == sizeof(udp_packet_t)) {
    udp_packet_t* pkt = (udp_packet_t*)data;
    switch (pkt->type) {
      case 0x01:  // 控制
        udp_service_push_cmd(pkt->motor1, pkt->motor2);
        break;
      case 0x03:  // 模式
        if (pkt->cmd <= 4) robot_mgr_post_mode((CarStatus)pkt->cmd, MODE_SRC_UDP);
        break;
      case 0x04:  // PID
        if (pkt->cmd == 0xFF) {
          /* 显式保存当前 PID 到 NV */
          mode_trace_save_pid();
        } else {
          mode_trace_set_pid(
              pkt->cmd, (int16_t)((pkt->motor1 << 8) | (uint8_t)pkt->motor2));
        }
        break;
      case 0xFE:  // 心跳包
        // 仅用于刷新超时时间，无其他业务逻辑
        break;

      case 0x05: {  // OTA 远程烧录
        udp_packet_t ack = {0};
        ack.type = 0x05;
        if (pkt->cmd == 0x01) {  // 启动 OTA
          if (ota_service_start(0)) {
            ack.cmd = 0x00;  // OK
          } else {
            ack.cmd = 0x01;  // Busy 或 Error
          }
        } else if (pkt->cmd == 0x02) {  // 取消 OTA
          ota_service_cancel();
          ack.cmd = 0x00;
        } else {
          ack.cmd = 0x02;  // 未知子命令
        }
        udp_net_common_send_to_addr(&ack, sizeof(ack), &g_server_addr);
        break;
      }
    }
  }
}

/**
 * @brief 接收处理
 */
static void handle_udp_receive(void) {
  uint8_t buf[128];
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  // 非阻塞接收
  int n = lwip_recvfrom(g_sockfd, buf, sizeof(buf), 0,
                        (struct sockaddr*)&client_addr, &addr_len);

  if (n > 0) {
    uint64_t now = osal_get_jiffies();

    // --- 单向触发重连：只要收到服务端任何指令包，立即进入连接态 ---
    if (!g_is_connected ||
        client_addr.sin_addr.s_addr != g_server_addr.sin_addr.s_addr ||
        client_addr.sin_port != g_server_addr.sin_port) {
      g_is_connected = true;
      memcpy_s(&g_server_addr, sizeof(g_server_addr), &client_addr,
               sizeof(client_addr));
      printf("[UDP] 建立连接/更新地址: %s\r\n",
             inet_ntoa(client_addr.sin_addr));
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
static void send_robot_state_or_heartbeat(void) {
  static RobotState last_sent_state = {0};
  RobotState curr;

  // 获取最新状态
  robot_mgr_get_state_copy(&curr);

  // 检查状态是否变化
  bool changed = (curr.mode != last_sent_state.mode ||
                  curr.distance != last_sent_state.distance ||
                  curr.ir_left != last_sent_state.ir_left ||
                  curr.ir_middle != last_sent_state.ir_middle ||
                  curr.ir_right != last_sent_state.ir_right);

  udp_packet_t pkt = {0};

  if (changed) {
    // 发送状态包 (Type 0x02)
    pkt.type = 0x02;
    pkt.cmd = curr.mode;
    pkt.motor1 = (int8_t)(curr.distance * 10);
    pkt.motor2 = 0;
    pkt.ir_data = (curr.ir_left & 1) | ((curr.ir_middle & 1) << 1) |
                  ((curr.ir_right & 1) << 2);

    last_sent_state = curr;
  } else {
    // 状态无变化，发送纯心跳包 (Type 0xFE)
    pkt.type = 0xFE;
    // 其他字段置0即可
  }

  // 定向发送给已连接的服务器
  udp_net_common_send_to_addr(&pkt, sizeof(pkt), &g_server_addr);
}

/**
 * @brief UDP 服务主任务
 */
static int udp_service_task(void* arg) {
  (void)arg;

  // 打开Socket，设置接收超时为 10ms
  g_sockfd =
      udp_net_common_open_and_bind(UDP_SERVER_PORT, UDP_RECV_TIMEOUT_MS, true);
  if (g_sockfd < 0) {
    printf("[UDP] Socket 创建失败\r\n");
    return 0;
  }

  uint64_t t_wifi_check = 0;
  uint64_t t_send_loop = 0;
  uint64_t t_keepalive_decay = 0;
  bool wifi_was_ready = false;

  while (!g_udp_should_exit) {
    uint64_t now = osal_get_jiffies();

    // 1. WiFi 状态维护 (每2秒检查一次)
    if (now - t_wifi_check >= osal_msecs_to_jiffies(2000)) {
      udp_net_common_wifi_ensure_connected();
      t_wifi_check = now;
    }

    bool wifi_ready = g_udp_net_wifi_connected && g_udp_net_wifi_has_ip;
    static bsp_wifi_mode_t last_mode = BSP_WIFI_MODE_STA;
    bsp_wifi_mode_t curr_mode = bsp_wifi_get_mode();

    static char last_ip[BUF_IP] = {0};

    /* 模式变化时重置边沿标记，确保切换后一定会刷新一次 */
    if (curr_mode != last_mode) {
      wifi_was_ready = false;
      last_ip[0] = '\0';
    }

    // WiFi 刚就绪或 IP 变化时刷新待机界面
    bool ip_changed = (wifi_ready && strcmp(last_ip, g_udp_net_ip) != 0);
    if ((wifi_ready && !wifi_was_ready) || ip_changed) {
      ui_show_mode_page(CAR_STOP_STATUS);
      wifi_was_ready = true;
      (void)strncpy_s(last_ip, sizeof(last_ip), g_udp_net_ip, sizeof(last_ip) - 1);
    } else if (!wifi_ready) {
      wifi_was_ready = false;
    }
    last_mode = curr_mode;

    if (wifi_ready) {
      // 2. 延迟构建广播包 (确保有MAC)
      if (!g_discovery_ready) {
        build_discovery_packet();
      }

      // 3. 容错计次衰减：每秒减少一次生命值
      if (now - t_keepalive_decay >= osal_msecs_to_jiffies(1000)) {
        t_keepalive_decay = now;
        if (g_is_connected && g_keepalive_count > 0) {
          g_keepalive_count--;
          // 生命值耗尽才真正断连
          if (g_keepalive_count == 0) {
            printf("[UDP] 连接超时 (5s内未收到心跳)，切回广播模式\r\n");
            g_is_connected = false;
            memset_s(&g_server_addr, sizeof(g_server_addr), 0,
                     sizeof(g_server_addr));
          }
        }
      }

      // 4. 梯度频率发送：广播期500ms，连接期2s
      uint32_t send_interval =
          g_is_connected ? CONNECTED_HEART_MS : BROADCAST_INTERVAL_MS;
      if (now - t_send_loop >= osal_msecs_to_jiffies(send_interval)) {
        t_send_loop = now;

        if (g_is_connected) {
          // --- 状态 B: 连接成功 (低频心跳 2s) ---
          send_robot_state_or_heartbeat();
        } else if (g_discovery_ready) {
          // --- 状态 A: 未连接 (高频广播 500ms) ---
          udp_net_common_send_broadcast(
              &g_discovery_pkt, sizeof(g_discovery_pkt), UDP_BROADCAST_PORT);
        }
      }
    } else {
      // WiFi未就绪，重置状态
      g_is_connected = false;
      g_discovery_ready = false;
      g_keepalive_count = KEEPALIVE_MAX_COUNT;
    }

    // 5. 接收处理 (此处会阻塞10ms)
    handle_udp_receive();
  }

  if (g_sockfd >= 0) {
    lwip_close(g_sockfd);
    g_sockfd = -1;
  }
  g_udp_task = NULL;
  return 0;
}