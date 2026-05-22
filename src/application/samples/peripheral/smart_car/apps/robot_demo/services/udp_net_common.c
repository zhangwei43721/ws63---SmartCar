#include "udp_net_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netifapi.h"
#include "securec.h"
#include "soc_osal.h"
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

/* STA 断线重试计数：连续失败 N 次后切回 AP */
#define STA_RETRY_MAX 3
static int g_sta_retry_count = 0;

/* 模式变化检测：切换模式时重置重试计时 */
static bsp_wifi_mode_t g_last_checked_mode = BSP_WIFI_MODE_STA;

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

/**
 * @brief 获取本机 WiFi MAC 地址
 * @param mac_buf 输出的 MAC 地址缓冲区（至少 6 字节）
 * @return 成功返回 0，失败返回 -1
 * @note 优先从 wlan0 获取，失败则尝试 ap0（AP 模式）
 */
int udp_net_get_mac_address(uint8_t* mac_buf) {
  if (!mac_buf) return -1;

  /* 优先尝试 STA 接口 */
  struct netif* netif_p = netifapi_netif_find("wlan0");
  if (netif_p == NULL) {
    /* AP 模式下尝试 ap0 */
    netif_p = netifapi_netif_find("ap0");
  }

  if (netif_p) {
    memcpy_s(mac_buf, 6, netif_p->hwaddr, 6);
    return 0;
  }
  return -1;
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

/* 维护 WiFi 连接状态：两种模式，来回切换 */
void udp_net_common_wifi_ensure_connected(void) {
  /* 模式变化检测：重置重试计时，避免切换后立即重连 */
  bsp_wifi_mode_t curr_mode = bsp_wifi_get_mode();
  if (curr_mode != g_last_checked_mode) {
    g_wifi_last_retry = 0;
    g_sta_retry_count = 0;
    g_last_checked_mode = curr_mode;
  }

  if (!g_wifi_inited) {
    if (bsp_wifi_smart_init() != 0) return;
    g_wifi_inited = true;
    g_wifi_last_retry = 0;
    g_sta_retry_count = 0;
    return;
  }

  /* ---------- AP 模式 ---------- */
  if (curr_mode == BSP_WIFI_MODE_AP) {
    g_udp_net_wifi_connected = true;
    g_sta_retry_count = 0;
    if (!g_udp_net_wifi_has_ip &&
        bsp_wifi_get_ip(g_udp_net_ip, sizeof(g_udp_net_ip)) == 0) {
      g_udp_net_wifi_has_ip = true;
      printf("udp_net: AP IP=%s\r\n", g_udp_net_ip);
    }
    return;
  }

  /* ---------- STA 模式 ---------- */
  bsp_wifi_status_t status = bsp_wifi_get_status();

  /* 1. 已就绪：有 IP 就完事 */
  if (status == BSP_WIFI_STATUS_GOT_IP) {
    g_udp_net_wifi_connected = true;
    g_sta_retry_count = 0;
    char new_ip[BUF_IP] = {0};
    bsp_wifi_get_ip(new_ip, sizeof(new_ip));
    if (!g_udp_net_wifi_has_ip || strcmp(g_udp_net_ip, new_ip) != 0) {
      (void)strncpy_s(g_udp_net_ip, sizeof(g_udp_net_ip), new_ip,
                      sizeof(g_udp_net_ip) - 1);
      printf("udp_net: STA 就绪 IP=%s\r\n", g_udp_net_ip);
    }
    g_udp_net_wifi_has_ip = true;
    return;
  }

  /* 2. 连接中或已关联但未获取IP：给DHCP留出时间，不重连 */
  if (status == BSP_WIFI_STATUS_CONNECTING ||
      status == BSP_WIFI_STATUS_CONNECTED) {
    g_udp_net_wifi_connected = false;
    g_udp_net_wifi_has_ip = false;
    return;
  }

  /* 3. 已断开：标记状态，定时重连 */
  g_udp_net_wifi_connected = false;
  g_udp_net_wifi_has_ip = false;

  unsigned int now = (unsigned int)osal_get_jiffies();
  if (g_wifi_last_retry != 0 &&
      (now - g_wifi_last_retry < osal_msecs_to_jiffies(5000))) {
    return;
  }
  g_wifi_last_retry = now;

  printf("udp_net: STA 断开，尝试重连...\r\n");
  if (wifi_connect_from_nv() == 0) return;

  /* 4. 重连失败，计数 */
  g_sta_retry_count++;
  printf("udp_net: STA 重连失败 (第%d次)\r\n", g_sta_retry_count);
  if (g_sta_retry_count >= STA_RETRY_MAX) {
    printf("udp_net: STA 连续 %d 次失败，切 AP\r\n", STA_RETRY_MAX);
    if (bsp_wifi_switch_to_ap() == 0) g_sta_retry_count = 0;
  }
}