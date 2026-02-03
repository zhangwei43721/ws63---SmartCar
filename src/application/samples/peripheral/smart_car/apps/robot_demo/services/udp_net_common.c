#include "udp_net_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "lwip/inet.h"
#include "securec.h"
#include "storage_service.h"

static osal_mutex g_net_mutex;
static bool g_net_mutex_inited = false;

#define NET_LOCK() MUTEX_LOCK(g_net_mutex, g_net_mutex_inited)
#define NET_UNLOCK() MUTEX_UNLOCK(g_net_mutex, g_net_mutex_inited)

static bool g_wifi_inited = false;
bool g_udp_net_wifi_connected = false;
bool g_udp_net_wifi_has_ip = false;
char g_udp_net_ip[BUF_IP] = "0.0.0.0";
static unsigned int g_wifi_last_retry = 0;

int g_udp_net_socket_fd = -1;
bool g_udp_net_bound = false;

/* 8位累加校验和 */
uint8_t udp_net_common_checksum8_add(const uint8_t* data, size_t len) {
  uint8_t sum = 0;
  if (data && len) {
    for (size_t i = 0; i < len; i++) sum += data[i];
  }
  return sum;
}

/* 内部辅助：发送数据到指定地址 */
static int send_udp_raw(const void* buf, size_t len,
                        const struct sockaddr_in* addr) {
  if (!buf || len == 0 || !addr) return -1;
  NET_LOCK();
  int fd = g_udp_net_socket_fd;
  bool bound = g_udp_net_bound;
  NET_UNLOCK();
  return (bound && fd >= 0)
             ? (int)lwip_sendto(fd, buf, len, 0, (struct sockaddr*)addr,
                                sizeof(*addr))
             : -1;
}

int udp_net_common_send_broadcast(const void* buf, size_t len, uint16_t port) {
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = lwip_htons(port);
  addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);
  return send_udp_raw(buf, len, &addr);
}

int udp_net_common_send_to_addr(const void* buf, size_t len,
                                const struct sockaddr_in* addr) {
  return send_udp_raw(buf, len, addr);
}

void udp_net_common_init(void) {
  if (!g_net_mutex_inited && osal_mutex_init(&g_net_mutex) == OSAL_SUCCESS) {
    g_net_mutex_inited = true;
  }
}

int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms,
                                 bool enable_broadcast) {
  int sockfd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) return -1;

  struct timeval tv = {0, (int)recv_timeout_ms * 1000};
  lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (enable_broadcast) {
    int broadcast = 1;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast,
                    sizeof(broadcast));
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = lwip_htons(port);
  addr.sin_addr.s_addr = IPADDR_ANY;

  if (lwip_bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("udp_net: Bind failed %d\r\n", errno);
    lwip_close(sockfd);
    return -1;
  }

  NET_LOCK();
  g_udp_net_socket_fd = sockfd;
  g_udp_net_bound = true;
  NET_UNLOCK();
  return sockfd;
}

/* 尝试从NV读取配置连接，否则用默认 */
static int wifi_connect_from_nv(void) {
  char ssid[32], password[64];
  storage_service_get_wifi_config(ssid, password);
  const char* s = (strlen(ssid) > 0) ? ssid : BSP_WIFI_AP_SSID;
  const char* p = (strlen(ssid) > 0) ? password : BSP_WIFI_AP_PASSWORD;
  return bsp_wifi_start_sta_with_timeout(s, p, 3000);
}

/* 维护 WiFi 连接状态（核心逻辑） */
void udp_net_common_wifi_ensure_connected(void) {
  if (!g_wifi_inited) {
    if (bsp_wifi_smart_init() != 0) return; /* 智能启动：STA失败自动切AP */
    g_wifi_inited = true;
    g_wifi_last_retry = 0;
  }

  /* AP模式：始终视为已连接，仅检查IP */
  if (bsp_wifi_get_mode() == BSP_WIFI_MODE_AP) {
    g_udp_net_wifi_connected = true;
    if (!g_udp_net_wifi_has_ip &&
        bsp_wifi_get_ip(g_udp_net_ip, sizeof(g_udp_net_ip)) == 0) {
      g_udp_net_wifi_has_ip = true;
      printf("udp_net: AP IP=%s\r\n", g_udp_net_ip);
    }
    return;
  }

  /* STA模式：断线重连逻辑 */
  unsigned int now = (unsigned int)osal_get_jiffies();
  if (!g_udp_net_wifi_connected) {
    if (g_wifi_last_retry == 0 || (now - g_wifi_last_retry >= 5000)) {
      g_wifi_last_retry = now;
      if (wifi_connect_from_nv() == 0) g_udp_net_wifi_connected = true;
    }
  }

  /* 更新状态标记 */
  if (g_udp_net_wifi_connected) {
    bsp_wifi_status_t status = bsp_wifi_get_status();
    if (status == BSP_WIFI_STATUS_GOT_IP) {
      if (bsp_wifi_get_ip(g_udp_net_ip, sizeof(g_udp_net_ip)) == 0)
        g_udp_net_wifi_has_ip = true;
    } else if (status == BSP_WIFI_STATUS_DISCONNECTED) {
      g_udp_net_wifi_connected = false;
      g_udp_net_wifi_has_ip = false;
    }
  }
}