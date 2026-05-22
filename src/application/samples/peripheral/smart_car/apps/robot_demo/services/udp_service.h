#ifndef UDP_SERVICE_H
#define UDP_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../robot_common.h"
#include "errcode.h"

#define UDP_SERVER_PORT 8888  // UDP 监听端口
#define UDP_STACK_SIZE 8192   // UDP 任务栈大小
#define UDP_TASK_PRIORITY 24  // UDP 任务优先级

#define UDP_BUFFER_SIZE 2048     // UDP 缓冲区大小
#define UDP_BROADCAST_PORT 8889  // UDP 广播端口

// WiFi配置命令 (0xE0 ~ 0xE2)
#define UDP_CMD_WIFI_CONFIG_SET 0xE0  // 设置STA模式WiFi配置（保存，不立即切换）
#define UDP_CMD_WIFI_CONFIG_CONNECT 0xE1  // 连接到指定WiFi并切换到STA模式
#define UDP_CMD_WIFI_CONFIG_GET 0xE2      // 获取当前WiFi配置

void udp_service_init(void);
void udp_service_deinit(void);
WifiConnectStatus udp_service_get_wifi_status(void);
const char* udp_service_get_ip(void);
void udp_service_push_cmd(int8_t motor1, int8_t motor2);

#endif
