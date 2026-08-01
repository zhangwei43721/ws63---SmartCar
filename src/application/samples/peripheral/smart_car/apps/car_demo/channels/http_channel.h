#ifndef HTTP_CHANNEL_H
#define HTTP_CHANNEL_H

#include <stdbool.h>

// HTTP 控制通道（AP 模式强制门户的 REST 接口）：
// 把 /api/* 请求翻译为控制中枢的统一意图，与 UDP/SLE 走同一仲裁路径
bool http_channel_handle(int client_fd, bool is_get, const char *path, const char *query);

#endif
