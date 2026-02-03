/**
 ****************************************************************************************************
 * @file        bsp_wifi.c
 * @author      SkyForever
 * @version     V1.4
 * @date        2025-02-03
 * @brief       WiFi连接BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 */

#include "bsp_wifi.h"

#include <stdio.h>
#include <string.h>

#include "../../apps/robot_demo/services/storage_service.h"
#include "cmsis_os2.h"
#include "lwip/netifapi.h"
#include "securec.h"
#include "soc_osal.h"
#include "td_type.h"
#include "wifi_device.h"
#include "wifi_event.h"
#include "wifi_hotspot.h"

/* 全局状态变量 */
static bsp_wifi_status_t g_wifi_status = BSP_WIFI_STATUS_IDLE;  // WiFi 连接状态
static bsp_wifi_mode_t g_wifi_mode = BSP_WIFI_MODE_STA;         // WiFi 工作模式

/**
 * @brief WiFi 连接事件回调函数
 * @param state WiFi 状态变化
 * @param info 连接信息（SSID、RSSI 等）
 * @param reason 断开原因（未使用）
 * @note 当 WiFi 连接状态改变时，由系统自动调用
 */
static void wifi_cb(td_s32 state, const wifi_linked_info_stru* info,
                    td_s32 reason) {
  (void)reason;
  if (state == WIFI_STATE_AVALIABLE) {
    printf("[WiFi] 已连接: %s, 信号强度: %d\r\n", info->ssid, info->rssi);
    g_wifi_status = BSP_WIFI_STATUS_CONNECTED;
  } else if (state == WIFI_STATE_NOT_AVALIABLE) {
    g_wifi_status = BSP_WIFI_STATUS_DISCONNECTED;
  }
}

/* WiFi 事件回调结构体 */
static wifi_event_stru g_wifi_event = {.wifi_event_connection_changed =
                                           wifi_cb};

/**
 * @brief 等待并获取 DHCP 分配的 IP 地址
 * @param ifname 网络接口名称（如 "wlan0"）
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回 0，失败返回 -1
 * @note 启动 DHCP 并等待获取有效 IP 地址
 */
static int wait_for_dhcp(const char* ifname, uint32_t timeout_ms) {
  struct netif* netif_p = netifapi_netif_find(ifname);
  if (!netif_p || netifapi_dhcp_start(netif_p) != ERR_OK) return -1;

  /* 等待 DHCP 绑定 */
  for (int i = 0; i < 20; i++) {
    if (netifapi_dhcp_is_bound(netif_p) == ERR_OK) break;
    osDelay(50);
  }

  /* 等待获取有效 IP 地址 */
  unsigned long long start = osal_get_jiffies();
  unsigned long long timeout_ticks = osal_msecs_to_jiffies(timeout_ms);

  while ((osal_get_jiffies() - start) < timeout_ticks) {
    if (netif_p->ip_addr.u_addr.ip4.addr != 0) {
      g_wifi_status = BSP_WIFI_STATUS_GOT_IP;
      return 0;
    }
    osDelay(10);
  }
  return -1;
}

/**
 * @brief 扫描并查找指定的 AP
 * @param ssid 目标 WiFi 名称
 * @param key WiFi 密码
 * @param cfg 输出的 WiFi 配置结构体
 * @return 成功返回 ERRCODE_SUCC，失败返回错误码
 * @note 扫描周围 WiFi，查找匹配的 SSID 并填充配置信息
 */
static errcode_t find_ap(const char* ssid, const char* key,
                         wifi_sta_config_stru* cfg) {
  uint32_t num = 32;

  /* 在堆上申请内存（避免栈溢出） */
  wifi_scan_info_stru* scan_res = (wifi_scan_info_stru*)osal_kmalloc(
      sizeof(wifi_scan_info_stru) * num, OSAL_GFP_ATOMIC);
  if (scan_res == NULL) {
    printf("[WiFi] 扫描内存分配失败\r\n");
    return ERRCODE_MALLOC;
  }

  /* 执行 WiFi 扫描 */
  wifi_sta_scan();
  osDelay(600);

  /* 获取扫描结果 */
  errcode_t ret = wifi_sta_get_scan_info(scan_res, &num);
  if (ret != ERRCODE_SUCC) {
    osal_kfree(scan_res);
    return ret;
  }

  /* 查找目标 SSID */
  errcode_t found = ERRCODE_FAIL;
  for (uint32_t i = 0; i < num; i++) {
    if (strcmp(scan_res[i].ssid, ssid) == 0) {
      memcpy_s(cfg->ssid, WIFI_MAX_SSID_LEN, scan_res[i].ssid,
               WIFI_MAX_SSID_LEN);
      memcpy_s(cfg->bssid, WIFI_MAC_LEN, scan_res[i].bssid, WIFI_MAC_LEN);
      cfg->security_type = scan_res[i].security_type;
      memcpy_s(cfg->pre_shared_key, WIFI_MAX_KEY_LEN, key, strlen(key));
      cfg->ip_type = DHCP;
      found = ERRCODE_SUCC;
      break;
    }
  }

  /* 释放扫描结果内存 */
  osal_kfree(scan_res);
  return found;
}

/**
 * @brief 初始化 AP 模式（热点）
 * @return 成功返回 0，失败返回 -1
 * @note 将设备配置为 WiFi 热点，SSID: WS63_Robot
 *       - 密码为空时使用开放网络（无密码）
 *       - 密码非空时使用 WPA2 加密
 */
static int init_ap_mode(void) {
  printf("[WiFi] 正在启动 AP 热点模式...\r\n");

  softap_config_stru conf = {0};
  memcpy_s(conf.ssid, sizeof(conf.ssid), BSP_WIFI_AP_SSID,
           strlen(BSP_WIFI_AP_SSID));
  memcpy_s(conf.pre_shared_key, WIFI_MAX_KEY_LEN, BSP_WIFI_AP_PASSWORD,
           strlen(BSP_WIFI_AP_PASSWORD));

  /* 根据密码是否为空自动选择安全模式 */
  if (strlen(BSP_WIFI_AP_PASSWORD) == 0) {
    conf.security_type = 0; /* 开放网络（无密码） */
    printf("[WiFi] 配置为开放网络（无密码）\r\n");
  } else {
    conf.security_type = 3; /* WPA2-PSK 加密 */
    printf("[WiFi] 配置为 WPA2 加密网络\r\n");
  }
  conf.channel_num = BSP_WIFI_AP_CHANNEL;

  /* 配置热点高级参数 */
  softap_config_advance_stru adv = {0};
  adv.beacon_interval = 100;
  adv.protocol_mode = 4;
  wifi_set_softap_config_advance(&adv);

  if (wifi_softap_enable(&conf) != 0) {
    printf("[WiFi] AP 热点启动失败\r\n");
    return -1;
  }

  /* 根据安全类型输出不同的日志 */
  if (conf.security_type == 0) {
    printf("[WiFi] AP 热点已启用: SSID=%s, 开放网络(无密码), 频道=%d\r\n",
           BSP_WIFI_AP_SSID, BSP_WIFI_AP_CHANNEL);
  } else {
    printf("[WiFi] AP 热点已启用: SSID=%s, 密码=%s, 频道=%d\r\n",
           BSP_WIFI_AP_SSID, BSP_WIFI_AP_PASSWORD, BSP_WIFI_AP_CHANNEL);
  }

  /* 设置静态 IP 地址 */
  struct netif* netif_p = netifapi_netif_find("ap0");
  if (netif_p) {
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, 192, 168, 43, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 43, 1);

    netifapi_netif_set_addr(netif_p, &ip, &mask, &gw);
    netifapi_dhcps_start(netif_p, NULL, 0);
    g_wifi_status = BSP_WIFI_STATUS_GOT_IP;
    printf("[WiFi] AP 热点 IP: 192.168.43.1\r\n");
    return 0;
  }
  printf("[WiFi] AP 热点网络接口配置失败\r\n");
  return -1;
}

/**
 * @brief WiFi 模块通用初始化接口
 * @param mode WiFi 工作模式（STA 或 AP）
 * @return 成功返回 0，失败返回 -1
 */
int bsp_wifi_init_ex(bsp_wifi_mode_t mode) {
  g_wifi_mode = mode;
  if (mode == BSP_WIFI_MODE_STA) wifi_register_event_cb(&g_wifi_event);
  while (!wifi_is_wifi_inited()) osDelay(10);

  if (mode == BSP_WIFI_MODE_STA) {
    return (wifi_sta_enable() == ERRCODE_SUCC) ? 0 : -1;
  } else {
    return init_ap_mode();
  }
}

/**
 * @brief 连接到指定的 STA 网络（带超时）
 * @param ssid WiFi 名称
 * @param password WiFi 密码
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回 0，失败返回 -1
 * @note 扫描并连接到指定的 AP，等待 DHCP 完成
 */
int bsp_wifi_start_sta_with_timeout(const char* ssid, const char* password,
                                    uint32_t timeout_ms) {
  if (!ssid || !password) return -1;
  g_wifi_status = BSP_WIFI_STATUS_CONNECTING;
  g_wifi_mode = BSP_WIFI_MODE_STA;

  wifi_sta_config_stru cfg = {0};
  unsigned long long start = osal_get_jiffies();
  unsigned long long timeout_ticks = osal_msecs_to_jiffies(timeout_ms);

  printf("[WiFi] 正在连接 %s (超时=%dms)...\r\n", ssid, timeout_ms);

  /* 循环尝试连接直到超时 */
  while ((osal_get_jiffies() - start) < timeout_ticks) {
    if (find_ap(ssid, password, &cfg) == ERRCODE_SUCC) {
      if (wifi_sta_connect(&cfg) == ERRCODE_SUCC) {
        /* 等待连接成功 */
        for (int i = 0; i < 40; i++) {
          if (g_wifi_status == BSP_WIFI_STATUS_CONNECTED) goto connected;
          osDelay(50);
        }
      }
    }
    osDelay(500);
  }
  return -1;

connected:
  return wait_for_dhcp("wlan0", 5000);
}

/**
 * @brief 兼容旧接口的连接函数
 * @param ssid WiFi 名称
 * @param password WiFi 密码
 * @return 成功返回 0，失败返回 -1
 * @note 默认超时时间为 20 秒
 */
int bsp_wifi_connect_ap(const char* ssid, const char* password) {
  return bsp_wifi_start_sta_with_timeout(ssid, password, 20000);
}

/**
 * @brief 智能启动模式：先尝试 STA，失败后自动切换 AP
 * @return 成功返回 0，失败返回 -1
 * @note 从 NV 读取 WiFi 配置，尝试连接；超时后切换为 AP 模式
 */
int bsp_wifi_smart_init(void) {
  char ssid[32] = {0}, pass[64] = {0};
  storage_service_get_wifi_config(ssid, pass);

  printf("[WiFi] 智能模式启动...\r\n");
  if (ssid[0] == '\0') {
    printf("[WiFi] NV 中无 WiFi 配置，直接启动 AP 模式\r\n");
    bsp_wifi_init_ex(BSP_WIFI_MODE_AP);
    return 0;
  }

  printf("[WiFi] NV 配置: SSID='%s'，尝试 STA 连接...\r\n", ssid);
  bsp_wifi_init_ex(BSP_WIFI_MODE_STA);

  /* 如果有配置的 SSID，尝试连接 */
  if (bsp_wifi_start_sta_with_timeout(ssid, pass, 15000) == 0) {
    printf("[WiFi] STA 连接成功\r\n");
    return 0;
  }

  /* STA 连接失败，切换到 AP 模式 */
  printf("[WiFi] STA 连接失败（密码错误或网络不可达），切换到 AP 模式\r\n");
  wifi_sta_disable();
  osDelay(200); /* 等待 STA 完全关闭 */
  g_wifi_mode = BSP_WIFI_MODE_AP;
  return init_ap_mode();
}

/**
 * @brief 从 AP 模式切换到 STA 模式
 * @param ssid 目标 WiFi 名称
 * @param password WiFi 密码
 * @return 成功返回 0，失败返回 -1
 * @note 关闭 AP 热点，切换为 STA 客户端模式并连接
 */
int bsp_wifi_switch_from_ap_to_sta(const char* ssid, const char* password) {
  /* 关闭 AP 热点 */
  wifi_softap_disable();
  struct netif* ap = netifapi_netif_find("ap0");
  if (ap) netifapi_dhcps_stop(ap);

  /* 启用 STA 模式 */
  if (wifi_sta_enable() != ERRCODE_SUCC) {
    bsp_wifi_smart_init();
    return -1;
  }

  /* 连接到指定 WiFi */
  if (bsp_wifi_start_sta_with_timeout(ssid, password, 15000) != 0) {
    /* 连接失败，回滚到 AP 模式 */
    wifi_sta_disable();
    init_ap_mode();
    return -1;
  }
  return 0;
}

/**
 * @brief 获取当前 WiFi 状态
 * @return WiFi 连接状态枚举值
 */
bsp_wifi_status_t bsp_wifi_get_status(void) { return g_wifi_status; }

/**
 * @brief 获取当前 WiFi 工作模式
 * @return WiFi 模式枚举值（STA 或 AP）
 */
bsp_wifi_mode_t bsp_wifi_get_mode(void) { return g_wifi_mode; }

/**
 * @brief 获取当前设备的 IP 地址字符串
 * @param ip_str 输出的 IP 地址字符串缓冲区
 * @param len 缓冲区长度
 * @return 成功返回 0，失败返回 -1
 * @note 根据当前模式返回对应网口的 IP（ap0 或 wlan0）
 */
int bsp_wifi_get_ip(char* ip_str, uint32_t len) {
  if (ip_str == NULL || len < 16) return -1;

  /* 初始化默认值 */
  strncpy_s(ip_str, len, "0.0.0.0", 7);

  /* 根据模式选择网络接口 */
  const char* name = (g_wifi_mode == BSP_WIFI_MODE_AP) ? "ap0" : "wlan0";

  /* 使用线程安全的 API 获取 netif */
  struct netif* netif_p = netifapi_netif_find(name);

  if (netif_p != NULL) {
    /* 读取 IP 地址（确保地址有效） */
    uint32_t addr = netif_p->ip_addr.u_addr.ip4.addr;
    if (addr != 0) {
      snprintf(ip_str, len, "%u.%u.%u.%u", (unsigned int)(addr & 0xFF),
               (unsigned int)((addr >> 8) & 0xFF),
               (unsigned int)((addr >> 16) & 0xFF),
               (unsigned int)((addr >> 24) & 0xFF));
      return 0;
    }
  }
  return -1;
}
