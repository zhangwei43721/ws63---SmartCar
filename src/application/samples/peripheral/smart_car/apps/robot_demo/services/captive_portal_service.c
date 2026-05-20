/**
 * @file        captive_portal_service.c
 * @brief       AP 配网服务（Captive Portal）实现
 * @details     在 AP 模式下启动轻量级 HTTP 服务器，提供 Web 配网页面。
 *              用户连接小车热点后访问 http://192.168.1.1/ 即可配置 WiFi。
 */

#include "captive_portal_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "../core/robot_config.h"
#include "securec.h"
#include "soc_osal.h"
#include "storage_service.h"

/* ---------- 配置常量 ---------- */
#define CAPTIVE_PORTAL_HTTP_PORT    80
#define CAPTIVE_PORTAL_STACK_SIZE   8192
#define CAPTIVE_PORTAL_TASK_PRIO    23
#define CAPTIVE_HTTP_RECV_TIMEOUT_MS 2000
#define CAPTIVE_HTTP_BUF_SIZE       1536
#define CAPTIVE_HTTP_MAX_BODY       512

/* ---------- 状态枚举 ---------- */
typedef enum {
    PORTAL_STATUS_IDLE = 0,
    PORTAL_STATUS_RUNNING,
    PORTAL_STATUS_CONFIG_RECEIVED,
    PORTAL_STATUS_SWITCHING,
    PORTAL_STATUS_SUCCESS,
    PORTAL_STATUS_FAILED
} portal_status_t;

/* ---------- 全局状态 ---------- */
static volatile portal_status_t g_portal_status = PORTAL_STATUS_IDLE;
static osal_task *g_portal_task = NULL;
static bool      g_task_should_exit = false;
static char      g_ap_ip_str[BUF_IP] = "0.0.0.0";
static char      g_status_text[32]   = "等待配网";

/* ---------- 内嵌配网页面（精简版） ---------- */
static const char s_html_page[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>小车WiFi配网</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#f2f3f5;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}"
    ".card{background:#fff;border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,.08);padding:28px;width:100%;max-width:360px}"
    "h1{font-size:20px;color:#1a1a1a;margin-bottom:4px;text-align:center}"
    ".sub{color:#888;font-size:13px;text-align:center;margin-bottom:20px}"
    ".field{margin-bottom:14px}"
    "label{display:block;font-size:13px;color:#555;margin-bottom:4px;font-weight:500}"
    "input{width:100%;padding:12px 14px;border:1px solid #ddd;border-radius:10px;font-size:15px;outline:0;transition:border .2s}"
    "input:focus{border-color:#007aff}"
    "button{width:100%;padding:13px;border:0;border-radius:10px;background:#007aff;color:#fff;font-size:16px;font-weight:600;cursor:pointer;margin-top:4px}"
    "button:active{opacity:.9}"
    ".tip{margin-top:14px;font-size:12px;color:#999;text-align:center;line-height:1.5}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>智能小车配网</h1>"
    "<p class=\"sub\">连接你的家庭WiFi，让小车接入局域网</p>"
    "<form method=\"POST\" action=\"/config\">"
    "<div class=\"field\">"
    "<label>WiFi 名称 (SSID)</label>"
    "<input type=\"text\" name=\"ssid\" placeholder=\"请输入WiFi名称\" required maxlength=31>"
    "</div>"
    "<div class=\"field\">"
    "<label>WiFi 密码</label>"
    "<input type=\"password\" name=\"password\" placeholder=\"请输入WiFi密码\" maxlength=63>"
    "</div>"
    "<button type=\"submit\">保存并连接</button>"
    "</form>"
    "<p class=\"tip\">提示：密码为空表示连接开放网络<br>配网成功后页面将自动跳转</p>"
    "</div></body></html>";

static const char s_html_success[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>配网成功</title>"
    "<style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#f2f3f5;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
    ".card{background:#fff;border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,.08);padding:32px;width:100%;max-width:320px;text-align:center}"
    "h1{font-size:22px;color:#34c759;margin-bottom:8px}"
    "p{color:#555;font-size:15px;line-height:1.6}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>配网成功!</h1>"
    "<p>小车正在连接WiFi...<br>请稍候，热点即将关闭。</p>"
    "</div></body></html>";

static const char s_html_fail[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>配网失败</title>"
    "<style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#f2f3f5;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
    ".card{background:#fff;border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,.08);padding:32px;width:100%;max-width:320px;text-align:center}"
    "h1{font-size:22px;color:#ff3b30;margin-bottom:8px}"
    "p{color:#555;font-size:15px;line-height:1.6}"
    "a{display:inline-block;margin-top:16px;padding:10px 20px;background:#007aff;color:#fff;text-decoration:none;border-radius:8px;font-size:15px}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>配网失败</h1>"
    "<p>无法连接到指定的WiFi<br>请检查SSID和密码是否正确</p>"
    "<a href=\"/\">返回重试</a>"
    "</div></body></html>";

static const char s_html_busy[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta http-equiv=\"refresh\" content=\"3;url=/\">"
    "<title>处理中</title>"
    "<style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#f2f3f5;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
    ".card{background:#fff;border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,.08);padding:32px;width:100%;max-width:320px;text-align:center}"
    "h1{font-size:20px;color:#ff9500;margin-bottom:8px}"
    "p{color:#555;font-size:15px}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>正在处理...</h1>"
    "<p>上一条配网请求正在执行中<br>请稍候</p>"
    "</div></body></html>";

/* ---------- 内部函数 ---------- */

/**
 * @brief URL 解码（仅处理 %XX 和 +）
 */
static int url_decode(const char *src, char *dst, size_t dst_len)
{
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j + 1 < dst_len) {
        if (src[i] == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
            unsigned int hex;
            if (sscanf(&src[i + 1], "%2x", &hex) == 1) {
                dst[j++] = (char)hex;
                i += 3;
                continue;
            }
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
            continue;
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
    return (int)j;
}

/**
 * @brief 从 application/x-www-form-urlencoded 数据中解析字段
 */
static bool parse_form_body(const char *body, char *ssid, size_t ssid_len,
                            char *password, size_t password_len)
{
    const char *p = body;
    bool got_ssid = false;

    ssid[0] = '\0';
    password[0] = '\0';

    while (p != NULL && *p != '\0') {
        const char *amp = strchr(p, '&');
        char pair[256] = {0};
        size_t pair_len = (amp != NULL) ? (size_t)(amp - p) : strlen(p);
        if (pair_len >= sizeof(pair)) pair_len = sizeof(pair) - 1;
        strncpy(pair, p, pair_len);
        pair[pair_len] = '\0';

        char *eq = strchr(pair, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = pair;
            char *val = eq + 1;
            char decoded[128] = {0};
            url_decode(val, decoded, sizeof(decoded));

            if (strcmp(key, "ssid") == 0) {
                strncpy(ssid, decoded, ssid_len - 1);
                ssid[ssid_len - 1] = '\0';
                got_ssid = true;
            } else if (strcmp(key, "password") == 0) {
                strncpy(password, decoded, password_len - 1);
                password[password_len - 1] = '\0';
            }
        }

        p = (amp != NULL) ? amp + 1 : NULL;
    }

    return got_ssid && (ssid[0] != '\0');
}

/**
 * @brief 发送 HTTP 响应并关闭 socket
 */
static void send_response_and_close(int client_fd, const char *response)
{
    if (client_fd >= 0) {
        (void)lwip_send(client_fd, response, strlen(response), 0);
        lwip_close(client_fd);
    }
}

/**
 * @brief 处理单个 HTTP 连接
 */
static void handle_http_client(int client_fd)
{
    char buf[CAPTIVE_HTTP_BUF_SIZE];
    int total = 0;
    int n;

    /* 设置接收超时 */
    struct timeval tv = {CAPTIVE_HTTP_RECV_TIMEOUT_MS / 1000,
                         (CAPTIVE_HTTP_RECV_TIMEOUT_MS % 1000) * 1000};
    lwip_setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 读取请求头 */
    while (total < (int)sizeof(buf) - 1) {
        n = lwip_recv(client_fd, buf + total, sizeof(buf) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        /* 检查是否收到完整请求头（\r\n\r\n） */
        if (strstr(buf, "\r\n\r\n") != NULL) break;
    }

    if (total <= 0) {
        lwip_close(client_fd);
        return;
    }
    buf[total] = '\0';

    /* 解析请求方法 */
    bool is_get  = (strncmp(buf, "GET ", 4) == 0);
    bool is_post = (strncmp(buf, "POST ", 5) == 0);

    /* 解析路径 */
    char path[32] = {0};
    const char *path_start = strchr(buf, ' ');
    if (path_start != NULL) {
        path_start++;
        const char *path_end = strchr(path_start, ' ');
        if (path_end != NULL) {
            size_t len = (size_t)(path_end - path_start);
            if (len >= sizeof(path)) len = sizeof(path) - 1;
            strncpy(path, path_start, len);
            path[len] = '\0';
        }
    }

    /* GET 请求返回配网页面 */
    if (is_get) {
        send_response_and_close(client_fd, s_html_page);
        return;
    }

    /* POST /config 处理配网 */
    if (is_post && strncmp(path, "/config", 7) == 0) {
        /* 检查是否正在处理其他配网请求 */
        if (g_portal_status == PORTAL_STATUS_CONFIG_RECEIVED ||
            g_portal_status == PORTAL_STATUS_SWITCHING) {
            send_response_and_close(client_fd, s_html_busy);
            return;
        }

        /* 解析 Content-Length */
        int content_len = 0;
        const char *cl = strstr(buf, "Content-Length:");
        if (cl != NULL) {
            sscanf(cl + 15, "%d", &content_len);
        }

        /* 提取已接收的 body */
        char *body_start = strstr(buf, "\r\n\r\n");
        int body_received = 0;
        char body[CAPTIVE_HTTP_MAX_BODY] = {0};

        if (body_start != NULL) {
            body_start += 4;
            body_received = total - (int)(body_start - buf);
            if (body_received > 0) {
                int copy_len = body_received;
                if (copy_len >= (int)sizeof(body)) copy_len = (int)sizeof(body) - 1;
                memcpy(body, body_start, copy_len);
                body[copy_len] = '\0';
            }
        }

        /* 如果 body 未收完，继续接收 */
        if (content_len > body_received && content_len < (int)sizeof(body)) {
            int need = content_len - body_received;
            n = lwip_recv(client_fd, body + body_received, need, 0);
            if (n > 0) {
                body_received += n;
                body[body_received] = '\0';
            }
        }

        /* 解析表单 */
        char ssid[32] = {0};
        char password[64] = {0};
        if (parse_form_body(body, ssid, sizeof(ssid), password, sizeof(password))) {
            printf("[Portal] 收到配网请求: SSID='%s', 密码长度=%zu\r\n",
                   ssid, strlen(password));

            g_portal_status = PORTAL_STATUS_CONFIG_RECEIVED;
            strncpy(g_status_text, "配网中...", sizeof(g_status_text));

            /* 先返回成功页面 */
            send_response_and_close(client_fd, s_html_success);

            /* 保存配置并切换 */
            storage_service_save_wifi_config(ssid, password);
            g_portal_status = PORTAL_STATUS_SWITCHING;
            strncpy(g_status_text, "切换STA", sizeof(g_status_text));

            printf("[Portal] 正在从 AP 切换到 STA 模式...\r\n");
            if (bsp_wifi_switch_from_ap_to_sta(ssid, password) == 0) {
                g_portal_status = PORTAL_STATUS_SUCCESS;
                strncpy(g_status_text, "配网成功", sizeof(g_status_text));
                printf("[Portal] 切换到 STA 成功\r\n");
            } else {
                g_portal_status = PORTAL_STATUS_FAILED;
                strncpy(g_status_text, "配网失败", sizeof(g_status_text));
                printf("[Portal] 切换到 STA 失败\r\n");
            }
            return;
        } else {
            send_response_and_close(client_fd, s_html_fail);
            return;
        }
    }

    /* 其他请求也返回配网页面 */
    send_response_and_close(client_fd, s_html_page);
}

/**
 * @brief 启动 HTTP 监听 socket
 */
static int http_server_start(void)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[Portal] socket 创建失败\r\n");
        return -1;
    }

    int opt = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(CAPTIVE_PORTAL_HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[Portal] bind 失败\r\n");
        lwip_close(fd);
        return -1;
    }

    if (lwip_listen(fd, 2) < 0) {
        printf("[Portal] listen 失败\r\n");
        lwip_close(fd);
        return -1;
    }

    printf("[Portal] HTTP 服务器已启动: port=%d\r\n", CAPTIVE_PORTAL_HTTP_PORT);
    return fd;
}

/**
 * @brief 关闭 HTTP 服务器
 */
static void http_server_stop(int fd)
{
    if (fd >= 0) {
        lwip_close(fd);
        printf("[Portal] HTTP 服务器已停止\r\n");
    }
}

/**
 * @brief 配网服务主任务
 */
static void *captive_portal_task(const char *arg)
{
    (void)arg;
    bool server_running = false;
    int fd = -1;

    while (!g_task_should_exit) {
        bsp_wifi_mode_t mode = bsp_wifi_get_mode();

        if (mode == BSP_WIFI_MODE_AP) {
            /* AP 模式下维持服务器 */
            if (!server_running) {
                fd = http_server_start();
                if (fd >= 0) {
                    server_running = true;
                    g_portal_status = PORTAL_STATUS_RUNNING;
                    strncpy(g_status_text, "等待配网", sizeof(g_status_text));

                    /* 更新 IP 显示 */
                    bsp_wifi_get_ip(g_ap_ip_str, sizeof(g_ap_ip_str));
                    printf("[Portal] AP IP: %s, 请用手机连接 %s 后访问 http://%s/\r\n",
                           g_ap_ip_str, BSP_WIFI_AP_SSID, g_ap_ip_str);
                }
            }

            if (server_running && fd >= 0) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                /* accept 带 1 秒超时，使任务可以定期检查退出标志 */
                struct timeval tv = {1, 0};
                lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

                int client_fd = lwip_accept(fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd >= 0) {
                    printf("[Portal] 客户端连接: %s\r\n",
                           inet_ntoa(client_addr.sin_addr));
                    handle_http_client(client_fd);
                }
            }
        } else {
            /* 非 AP 模式关闭服务器 */
            if (server_running) {
                http_server_stop(fd);
                fd = -1;
                server_running = false;
                g_portal_status = PORTAL_STATUS_IDLE;
            }
            /* 休眠更长，减少 CPU 占用 */
            osal_msleep(500);
        }

        osal_msleep(10);
    }

    if (server_running) {
        http_server_stop(fd);
    }
    g_portal_task = NULL;
    return NULL;
}

/* ---------- 公共接口 ---------- */

void captive_portal_service_init(void)
{
    if (g_portal_task != NULL) return;

    g_task_should_exit = false;

    osal_kthread_lock();
    g_portal_task = osal_kthread_create(
        (osal_kthread_handler)captive_portal_task, NULL,
        "portal_task", CAPTIVE_PORTAL_STACK_SIZE);
    if (g_portal_task != NULL) {
        osal_kthread_set_priority(g_portal_task, CAPTIVE_PORTAL_TASK_PRIO);
    }
    osal_kthread_unlock();

    if (g_portal_task != NULL) {
        printf("[Portal] 配网服务已初始化\r\n");
    } else {
        printf("[Portal] 配网服务任务创建失败\r\n");
    }
}

bool captive_portal_service_is_running(void)
{
    return (g_portal_status == PORTAL_STATUS_RUNNING ||
            g_portal_status == PORTAL_STATUS_CONFIG_RECEIVED ||
            g_portal_status == PORTAL_STATUS_SWITCHING);
}

const char* captive_portal_service_get_ap_ip(void)
{
    if (bsp_wifi_get_mode() == BSP_WIFI_MODE_AP) {
        bsp_wifi_get_ip(g_ap_ip_str, sizeof(g_ap_ip_str));
    }
    return g_ap_ip_str;
}

const char* captive_portal_service_get_status_text(void)
{
    return g_status_text;
}
