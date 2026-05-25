/**
 * @file        captive_portal_control.h
 * @brief       AP 模式下的小车 HTTP 控制接口
 * @details     提供 /control 页面和 /api/... REST 接口，手机连上小车热点后可直接控制
 */

#ifndef CAPTIVE_PORTAL_CONTROL_H
#define CAPTIVE_PORTAL_CONTROL_H

#include <stdbool.h>

/**
 * @brief 处理控制相关的 HTTP 请求
 * @param client_fd  客户端 socket
 * @param is_get     是否为 GET 请求
 * @param path       请求路径（如 "/control" 或 "/api/status"）
 * @param query      URL 查询字符串起始位置（path 中 '?' 之后），若无则为 NULL
 * @return true 表示请求已被处理并发送了响应；false 表示不是控制相关路径
 */
bool captive_portal_control_handle(int client_fd, bool is_get, const char *path, const char *query);

#endif /* CAPTIVE_PORTAL_CONTROL_H */
