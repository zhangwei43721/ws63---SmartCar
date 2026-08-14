/**
 * @file        http_channel.c
 * @brief       AP 模式下的小车 HTTP 控制通道实现
 * @details     内嵌控制页面 + /api/status /api/mode /api/move REST 接口；
 *              本通道只做 REST → 统一控制意图的翻译，决策交 core/car_ctrl
 */

#include "http_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../car_common.h"
#include "../core/car_ctrl.h"
#include "../core/car_state.h"
#include "lwip/sockets.h"
#include "../services/udp_net_common.h"
#include "../services/portal_html.h"

// ---------- 工具函数 ----------

/**
 * @brief 从 query string 中提取指定键的整数值
 * @param query  查询字符串，如 "m=1&r=100"
 * @param key    要查找的键，如 "m"
 * @return  解析到的整数值；若未找到返回 0
 */
static int query_get_int(const char *query, const char *key)
{
    if (query == NULL || key == NULL)
        return 0;

    size_t key_len = strlen(key);
    const char *p = query;

    while (*p != '\0') {
        // 跳过开头的 '?' 或 '&'
        if (*p == '?' || *p == '&')
            p++;

        // 检查是否匹配 key
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            return atoi(p + key_len + 1);
        }

        // 跳到下一个参数
        const char *amp = strchr(p, '&');
        if (amp != NULL) {
            p = amp + 1;
        } else {
            break;
        }
    }
    return 0;
}

// ---------- API 处理 ----------

/**
 * @brief GET /api/status -> 返回 JSON 状态
 */
static void handle_api_status(int client_fd)
{
    CarState st; // 模式状态
    car_state_get_copy(&st);

    char json[256];
    int json_len =
        snprintf(json, sizeof(json),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Connection: close\r\n\r\n"
                 "{\"mode\":%d,\"dist\":%d.%d,\"ir\":[%u,%u,%u]}\r\n",
                 (int)st.mode, (int)st.distance, ((int)(st.distance * 10)) % 10, st.ir_left, st.ir_middle, st.ir_right);
    if (json_len < 0 || (size_t)json_len >= sizeof(json)) {
        json[sizeof(json) - 1] = '\0';
    }

    http_send_response_and_close(client_fd, json);
}

/**
 * @brief GET /api/mode?m=X -> 设置小车模式
 */
static void handle_api_mode(int client_fd, const char *query)
{
    int mode = query_get_int(query, "m");
    if (mode >= 0 && mode <= 3) {
        // 与 UDP/SLE/语音一致：投统一命令总线，由 car_ctrl 任务串行仲裁
        car_cmd_t cmd = {
            .source = MODE_SRC_HTTP,
            .reply = NULL,
            .reply_ctx = NULL,
            .len = sizeof(car_packet_t),
            .data = {CAR_PKT_MODE, (uint8_t)mode, 0, 0, 0},
        };
        (void)car_ctrl_post_cmd(&cmd);
        printf("[Portal] HTTP 设置模式: %d\r\n", mode);
    }

    char json[128];
    (void)snprintf(json, sizeof(json),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Connection: close\r\n\r\n"
                   "{\"ok\":true,\"mode\":%d}\r\n",
                   mode);

    http_send_response_and_close(client_fd, json);
}

/**
 * @brief GET /api/move?l=X&r=Y -> 控制电机
 */
static void handle_api_move(int client_fd, const char *query)
{
    int left = query_get_int(query, "l");
    int right = query_get_int(query, "r");

    // 与 UDP/SLE/语音一致：投统一命令总线，由 car_ctrl 任务串行仲裁
    car_cmd_t cmd = {
        .source = MODE_SRC_HTTP,
        .reply = NULL,
        .reply_ctx = NULL,
        .len = sizeof(car_packet_t),
        .data = {CAR_PKT_CONTROL, 0, (uint8_t)(int8_t)left, (uint8_t)(int8_t)right, 0},
    };
    (void)car_ctrl_post_cmd(&cmd);

    char json[128];
    (void)snprintf(json, sizeof(json),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Connection: close\r\n\r\n"
                   "{\"ok\":true}\r\n");

    http_send_response_and_close(client_fd, json);
}

/**
 * @brief GET /api/reset -> 返回成功响应
 */
static void handle_api_reset(int client_fd)
{
    char json[128];
    (void)snprintf(json, sizeof(json),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Connection: close\r\n\r\n"
                   "{\"ok\":true}\r\n");

    http_send_response_and_close(client_fd, json);
}

// ---------- 公共接口 ----------

// 处理控制页面及 REST API 请求，匹配路径则处理并返回 true
bool http_channel_handle(int client_fd, bool is_get, const char *path, const char *query)
{
    if (is_get && strcmp(path, "/control") == 0) {
        http_send_html_response(client_fd, s_html_control);
        return true;
    }

    if (is_get && strcmp(path, "/api/status") == 0) {
        handle_api_status(client_fd);
        return true;
    }

    if (is_get && strcmp(path, "/api/mode") == 0) {
        handle_api_mode(client_fd, query);
        return true;
    }

    if (is_get && strcmp(path, "/api/move") == 0) {
        handle_api_move(client_fd, query);
        return true;
    }

    if (is_get && strcmp(path, "/api/reset") == 0) {
        handle_api_reset(client_fd);
        return true;
    }

    return false;
}
