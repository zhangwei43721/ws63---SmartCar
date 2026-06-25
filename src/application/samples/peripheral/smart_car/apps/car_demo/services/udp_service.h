#ifndef UDP_SERVICE_H
#define UDP_SERVICE_H

#include <stdint.h>
#include <stddef.h>

#define UDP_SERVER_PORT 8888    // UDP 监听端口
#define UDP_BROADCAST_PORT 8889 // UDP 广播端口

void udp_service_init(void); // 初始化 UDP 控制服务（创建监听任务）
void udp_service_send_data(const uint8_t *data, size_t len); // 向绑定的主机发送 UDP 数据包
void udp_service_send_trace_info(void); // 获取并向主机发送红外传感器原始电压和阈值

#endif
