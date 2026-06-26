#ifndef DEBUG_LOG_SERVICE_H
#define DEBUG_LOG_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

// 初始化调试日志服务
void debug_log_init(void);

// 线程安全的日志写入接口（打印到串口，同时通过 UDP 发送日志包）
void car_log(const char *fmt, ...);

#endif // DEBUG_LOG_SERVICE_H
