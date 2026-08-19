#ifndef UDP_CHANNEL_H
#define UDP_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#define UDP_SERVER_PORT 8888    // UDP 监听端口
#define UDP_BROADCAST_PORT 8889 // UDP 广播端口

// UDP 控制通道：广播发现 + 心跳维持 + 统一协议包接入（解析后交 core/car_ctrl 处理）
void udp_channel_init(void);                                 // 初始化 UDP 控制通道（创建监听任务）
void udp_channel_send_data(const uint8_t *data, size_t len); // 向绑定的主机发送 UDP 数据包
void udp_channel_send_trace_info(void);                      // 向主机发送红外传感器原始电压和阈值

#endif
