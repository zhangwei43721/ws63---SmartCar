#ifndef UDP_SERVICE_H
#define UDP_SERVICE_H

#define UDP_SERVER_PORT 8888    // UDP 监听端口
#define UDP_BROADCAST_PORT 8889 // UDP 广播端口

void udp_service_init(void); // 初始化 UDP 控制服务（创建监听任务）

#endif
