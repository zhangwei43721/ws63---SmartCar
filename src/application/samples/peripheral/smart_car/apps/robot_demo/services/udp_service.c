/**
 * @file        udp_service.c
 * @brief       UDP 通信服务实现
 * @details     提供 WiFi 遥控命令接收、状态上报、设备发现、心跳保活等功能
 * @date        2025-02-03
 */

#include "udp_service.h"

#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "../core/mode_trace.h"
#include "../core/robot_mgr.h"
#include "lwip/inet.h"        // inet_ntoa
#include "lwip/sockets.h"     // 套接字 API
#include "securec.h"          // memcpy_s
#include "storage_service.h"  // WiFi 配置存储
#include "udp_net_common.h"

/* --- 前向声明 --- */
static void* udp_service_task(const char* arg);
static void handle_udp_receive(int sockfd);
static void check_and_send_robot_state(uint64_t now, uint64_t* t_state,
                                       RobotState* last_state);

/**
 * @brief UDP 协议包定义（6 字节固定长度）
 * @details 支持控制命令、模式切换、PID 设置等
 */
typedef struct {
  uint8_t type;   // 包类型：01=控制, 02=状态, 03=模式, 04=PID, FE=心跳, FF=广播
  uint8_t cmd;    // 命令/模式编号
  int8_t motor1;  // 左电机值 (-100~100) 或复用
  int8_t motor2;  // 右电机值 (-100~100) 或复用
  int8_t ir_data;    // 红外传感器数据或扩展字段
  uint8_t checksum;  // 校验和（累加和）
} __attribute__((packed)) udp_packet_t;

/**
 * @brief 服务器信息维护结构体
 */
#define MAX_SERVERS 4
typedef struct {
  struct sockaddr_in addr;  // 服务器地址
  uint64_t last_heartbeat;  // 最后心跳时间
} server_info_t;

/* 全局变量 */
static server_info_t g_servers[MAX_SERVERS];  // 已连接的服务器列表
static int g_server_count = 0;                // 服务器数量
static uint8_t g_buf[UDP_BUFFER_SIZE];        // 全局缓冲区，避免栈溢出
static osal_mutex g_cmd_mutex;                // 命令缓存互斥锁
static struct {
  int8_t m1, m2;  // 电机命令缓存
  bool new_data;  // 是否有新数据
} g_cmd_cache = {0};

/**
 * @brief 向所有记录的客户端发送数据
 * @param buf 数据缓冲区
 * @param len 数据长度
 */
static void send_to_all(const void* buf, size_t len) {
  for (int i = 0; i < g_server_count; i++) {
    udp_net_common_send_to_addr(buf, len, &g_servers[i].addr);
  }
}

/**
 * @brief 构建并发送 UDP 数据包
 * @param type 包类型
 * @param cmd 命令字节
 * @param m1 电机1值
 * @param m2 电机2值
 * @param ir 红外数据
 * @details 支持广播或组发，根据是否有连接的服务器自动选择
 */
static void send_packet_wrapper(uint8_t type, uint8_t cmd, int8_t m1, int8_t m2,
                                int8_t ir) {
  udp_packet_t* pkt = (udp_packet_t*)g_buf;
  pkt->type = type;
  pkt->cmd = cmd;
  pkt->motor1 = m1;
  pkt->motor2 = m2;
  pkt->ir_data = ir;
  pkt->checksum = udp_net_common_checksum8_add((uint8_t*)pkt, 5);

  /* 如果没有服务器连接，或者是显式的广播包(0xFF)，则执行广播 */
  if (g_server_count == 0 || type == 0xFF)
    udp_net_common_send_broadcast(g_buf, 6, UDP_BROADCAST_PORT);
  else
    send_to_all(g_buf, 6);
}

/* -------------------------------------------------------------------------- */
/*                          外部接口实现                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化 UDP 服务
 * @details 初始化网络层、创建互斥锁、启动 UDP 任务线程
 */
void udp_service_init(void) {
  udp_net_common_init();
  osal_mutex_init(&g_cmd_mutex);
  osal_task* task = osal_kthread_create((osal_kthread_handler)udp_service_task,
                                        NULL, "udp_task", UDP_STACK_SIZE);
  if (task) osal_kthread_set_priority(task, UDP_TASK_PRIORITY);
}

/**
 * @brief 获取当前 WiFi 连接状态
 * @return WiFi 状态枚举值
 */
WifiConnectStatus udp_service_get_wifi_status(void) {
  if (bsp_wifi_get_mode() == BSP_WIFI_MODE_AP) return WIFI_STATUS_AP_MODE;
  if (g_udp_net_wifi_connected && g_udp_net_wifi_has_ip)
    return WIFI_STATUS_CONNECTED;
  return g_udp_net_wifi_connected ? WIFI_STATUS_CONNECTING
                                  : WIFI_STATUS_DISCONNECTED;
}

/**
 * @brief 获取当前设备的 IP 地址字符串
 * @return IP 地址字符串指针
 */
const char* udp_service_get_ip(void) { return g_udp_net_ip; }

/**
 * @brief 推送电机控制命令到缓存
 * @param m1 左电机值 (-100~100)
 * @param m2 右电机值 (-100~100)
 */
void udp_service_push_cmd(int8_t m1, int8_t m2) {
  osal_mutex_lock(&g_cmd_mutex);
  g_cmd_cache.m1 = m1;
  g_cmd_cache.m2 = m2;
  g_cmd_cache.new_data = true;
  osal_mutex_unlock(&g_cmd_mutex);
}

/**
 * @brief 从缓存中弹出电机控制命令
 * @param m1 输出的左电机值指针
 * @param m2 输出的右电机值指针
 * @return 有新数据返回 true，否则返回 false
 */
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
/*                          内部函数实现                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief 更新或添加客户端地址到服务器列表
 * @param addr 客户端地址
 * @details 如果客户端已存在则更新心跳时间，否则添加新客户端
 */
static void update_server_list(const struct sockaddr_in* addr) {
  uint64_t now = osal_get_jiffies();
  for (int i = 0; i < g_server_count; i++) {
    if (addr->sin_addr.s_addr == g_servers[i].addr.sin_addr.s_addr &&
        addr->sin_port == g_servers[i].addr.sin_port) {
      g_servers[i].last_heartbeat = now;
      return;
    }
  }
  if (g_server_count < MAX_SERVERS) {
    g_servers[g_server_count].addr = *addr;
    g_servers[g_server_count++].last_heartbeat = now;
    printf("[UDP] 新客户端连接: %s:%d (总数:%d)\r\n", inet_ntoa(addr->sin_addr),
           ntohs(addr->sin_port), g_server_count);
  }
}

/**
 * @brief 清理超过 60 秒未响应的客户端
 * @details 定期检查并移除超时的客户端
 */
static void cleanup_servers(void) {
  uint64_t now = osal_get_jiffies();
  for (int i = 0; i < g_server_count;) {
    if ((now - g_servers[i].last_heartbeat) > osal_msecs_to_jiffies(60000)) {
      printf("[UDP] 客户端超时: %s\r\n", inet_ntoa(g_servers[i].addr.sin_addr));
      g_servers[i] = g_servers[--g_server_count];
    } else
      i++;
  }
}

/**
 * @brief 处理 WiFi 配置指令 (0xE0-0xE2)
 * @param data 接收到的数据
 * @param len 数据长度
 * @param addr 发送方地址
 * @details 支持：0xE0=设置配置, 0xE1=设置并连接, 0xE2=获取配置
 */
static void handle_wifi_config(uint8_t* data, uint16_t len,
                               struct sockaddr_in* addr) {
  uint8_t cmd = data[0];

  /* 处理获取配置命令 (0xE2) */
  if (cmd == UDP_CMD_WIFI_CONFIG_GET) {
    char ssid[32], pass[64];
    storage_service_get_wifi_config(ssid, pass);
    uint8_t resp[100] = {UDP_CMD_WIFI_CONFIG_GET, (uint8_t)strlen(ssid)};
    uint16_t idx = 2;
    memcpy_s(resp + idx, sizeof(resp) - idx, ssid, strlen(ssid));
    idx += strlen(ssid);
    resp[idx++] = (uint8_t)strlen(pass);
    memcpy_s(resp + idx, sizeof(resp) - idx, pass, strlen(pass));
    idx += strlen(pass);
    resp[idx] = udp_net_common_checksum8_add(resp, idx);
    udp_net_common_send_to_addr(resp, idx + 1, addr);
    return;
  }

  /* 处理设置配置命令 (0xE0/0xE1) */
  if (len >= 4) {
    uint8_t slen = data[1], plen = data[2 + slen];
    if (slen > 31 || plen > 63 || (3 + slen + plen) >= len) return;
    char ssid[32] = {0}, pass[64] = {0};
    memcpy_s(ssid, 31, &data[2], slen);
    memcpy_s(pass, 63, &data[3 + slen], plen);

    if (cmd == UDP_CMD_WIFI_CONFIG_SET) {
      /* 仅保存配置，不切换模式 */
      printf("[UDP] WiFi 配置设置: SSID='%s'\r\n", ssid);
      errcode_t ret = storage_service_save_wifi_config(ssid, pass);
      printf("[UDP] 保存结果: %s\r\n", ret == ERRCODE_SUCC ? "成功" : "失败");
      uint8_t resp[] = {UDP_CMD_WIFI_CONFIG_SET,
                        ret == ERRCODE_SUCC ? 0 : 0xFF};
      udp_net_common_send_to_addr(resp, 2, addr);
    } else {
      /* 保存配置并切换到 STA 模式 */
      printf("[UDP] WiFi 配置并连接: SSID='%s'\r\n", ssid);
      storage_service_save_wifi_config(ssid, pass);
      int ret = bsp_wifi_switch_from_ap_to_sta(ssid, pass);
      uint8_t resp[] = {UDP_CMD_WIFI_CONFIG_CONNECT, ret == 0 ? 0 : 0xFF};
      udp_net_common_send_to_addr(resp, 2, addr);
    }
  }
}

/**
 * @brief 检查机器人状态并发送状态包
 * @param now 当前时间（jiffies）
 * @param t_state 上次发送状态的时间指针
 * @param last_state 上次发送的状态指针
 * @details 状态变化时立即发送，否则每 500ms 定期发送
 */
static void check_and_send_robot_state(uint64_t now, uint64_t* t_state,
                                       RobotState* last_state) {
  RobotState curr;
  robot_mgr_get_state_copy(&curr);
  bool changed =
      (curr.mode != last_state->mode || curr.distance != last_state->distance ||
       curr.ir_left != last_state->ir_left ||
       curr.ir_middle != last_state->ir_middle ||
       curr.ir_right != last_state->ir_right);

  if (changed || (now - *t_state >= 500)) {
    uint8_t ir = (curr.ir_left & 1) | ((curr.ir_middle & 1) << 1) |
                 ((curr.ir_right & 1) << 2);
    send_packet_wrapper(0x02, curr.mode, (int8_t)(curr.distance * 10), 0, ir);
    *last_state = curr;
    *t_state = now;
  }
}

/**
 * @brief 处理接收到的 UDP 数据
 * @param sockfd UDP 套接字描述符
 * @details 解析数据包并分发处理：控制命令、模式切换、PID 设置等
 */
static void handle_udp_receive(int sockfd) {
  struct sockaddr_in client;
  socklen_t len = sizeof(client);
  ssize_t n = lwip_recvfrom(sockfd, g_buf, sizeof(g_buf), 0,
                            (struct sockaddr*)&client, &len);

  if (n <= 0) return;

  /* 只要收到数据，就更新服务器活跃状态 */
  update_server_list(&client);

  /* 处理 WiFi 配置命令 (type >= 0xE0) */
  if (n >= 2 && g_buf[0] >= 0xE0) {
    handle_wifi_config(g_buf, (uint16_t)n, &client);
    return;
  }

  /* 处理标准控制命令 (6 字节) */
  if (n == 6) {
    udp_packet_t* pkt = (udp_packet_t*)g_buf;
    if (udp_net_common_checksum8_add(g_buf, 5) == pkt->checksum) {
      switch (pkt->type) {
        case 0x01: /* 电机控制命令 */
          udp_service_push_cmd(pkt->motor1, pkt->motor2);
          break;
        case 0x03: /* 模式切换命令 */
          if (pkt->cmd <= 4) robot_mgr_set_status((CarStatus)pkt->cmd);
          break;
        case 0x04: /* PID 参数设置命令 */
          mode_trace_set_pid(
              pkt->cmd, (int16_t)((pkt->motor1 << 8) | (uint8_t)pkt->motor2));
          break;
      }
    }
  }
}

/**
 * @brief UDP 服务主任务
 * @param arg 任务参数（未使用）
 * @details 循环处理：WiFi 保活、广播/心跳、状态上报、UDP 接收
 */
static void* udp_service_task(const char* arg) {
  (void)arg;
  int sockfd = udp_net_common_open_and_bind(UDP_SERVER_PORT, 100, true);
  if (sockfd < 0) return NULL;

  uint64_t t_last = 0, t_state = 0;
  RobotState last_state = {0};
  bool was_ready = false;

  while (1) {
    /* 确保 WiFi 连接 */
    udp_net_common_wifi_ensure_connected();
    bool ready = g_udp_net_wifi_connected && g_udp_net_wifi_has_ip;
    uint64_t now = osal_get_jiffies();

    /* 检测 WiFi 重连瞬间 */
    if (ready && !was_ready) {
      printf("[UDP] WiFi 已连接/已重新连接。重置广播服务器列表...\r\n");
      g_server_count = 0; /* 清空服务器，强制进入广播寻找状态 */
      t_last = 0;         /* 确保立即触发广播包 */
    }
    was_ready = ready;

    if (ready) {
      bool no_server = (g_server_count == 0);

      /* 1. 发送广播包 (无人连接时) 或 心跳包 (有人连接时) */
      if (no_server) {
        if (now - t_last >= 2000) { /* 每 2 秒广播一次发现包 */
          send_packet_wrapper(0xFF, 0, 0, 0, 0);
          t_last = now;
        }
      } else {
        if (now - t_last >= 5000) { /* 每 5 秒维护一次心跳和清理 */
          uint8_t hb[] = {0xFE, 0xFE};
          send_to_all(hb, 2);
          cleanup_servers();
          t_last = now;
        }
      }

      /* 2. 发送机器人状态同步包 */
      check_and_send_robot_state(now, &t_state, &last_state);
    }

    /* 3. 处理 UDP 接收 */
    handle_udp_receive(sockfd);
    osal_msleep(50);
  }
  return NULL;
}
