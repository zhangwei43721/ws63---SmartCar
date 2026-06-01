#ifndef UDP_SERVICE_H
#define UDP_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../car_common.h"

#define UDP_SERVER_PORT 8888 // UDP 监听端口

#define UDP_BROADCAST_PORT 8889 // UDP 广播端口

// WiFi配置命令 (0xE0 ~ 0xE2)
#define UDP_CMD_WIFI_CONFIG_SET 0xE0     // 设置STA模式WiFi配置（保存，不立即切换）
#define UDP_CMD_WIFI_CONFIG_CONNECT 0xE1 // 连接到指定WiFi并切换到STA模式
#define UDP_CMD_WIFI_CONFIG_GET 0xE2     // 获取当前WiFi配置

void udp_service_init(void);                         /* 初始化 UDP 控制服务（创建监听任务） */
WifiConnectStatus udp_service_get_wifi_status(void); /* 获取当前 WiFi 连接状态 */
const char *udp_service_get_ip(void);                /* 获取当前 IP 地址字符串 */

#endif
