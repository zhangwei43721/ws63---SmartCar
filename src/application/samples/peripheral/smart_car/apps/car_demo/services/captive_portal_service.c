/**
 * @file        captive_portal_service.c
 * @brief       AP 配网服务（Captive Portal）
 */

#include "captive_portal_service.h"
#include <stdio.h>
#include <string.h>
#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "../../../drivers/wifi_client/bsp_wifi_ap.h"
#include "wifi_mgr_service.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "../car_common.h"
#include "securec.h"
#include "soc_osal.h"
#include "../../../platform/storage_service.h"
#include "../channels/http_channel.h"
#include "udp_net_common.h"

#define PORTAL_STATIC_IP "192.168.1.1" // AP 模式固定 IP 地址

typedef enum {
    PORTAL_STATUS_IDLE = 0,        // 空闲：等待配网
    PORTAL_STATUS_RUNNING,         // 运行中：HTTP 服务已启动
    PORTAL_STATUS_CONFIG_RECEIVED, // 已收到 WiFi 配置
    PORTAL_STATUS_SWITCHING,       // 正在切换到 STA 模式
    PORTAL_STATUS_SUCCESS,         // 配网成功
    PORTAL_STATUS_FAILED           // 配网失败
} portal_status_t;                 // 门户服务定义

static volatile portal_status_t g_portal_status = PORTAL_STATUS_IDLE; // Portal 当前状态
static osal_task *g_portal_task = NULL;                               // Portal 任务句柄
static bool g_task_should_exit = false;                               // Portal 任务退出标志
static char g_status_text[32] = "等待配网";                           // Portal 状态显示文本

static osal_mutex g_portal_lock;          // Portal 共享状态互斥锁
static bool g_portal_lock_inited = false; // Portal 互斥锁是否已初始化

static osal_event g_portal_event;          // Portal 事件组（AP_READY/AP_STOPPED/EXIT）
static bool g_portal_event_inited = false; // Portal 事件组是否已初始化

// 加锁保护 Portal 共享状态
static inline void portal_lock(void)
{
    if (g_portal_lock_inited)
        (void)osal_mutex_lock(&g_portal_lock);
}
// 解锁 Portal 共享状态
static inline void portal_unlock(void)
{
    if (g_portal_lock_inited)
        (void)osal_mutex_unlock(&g_portal_lock);
}

// 设置 Portal 状态并更新状态文本
static void portal_set_status(portal_status_t st, const char *text)
{
    portal_lock();
    g_portal_status = st;
    if (text != NULL) {
        (void)strncpy_s(g_status_text, sizeof(g_status_text), text, sizeof(g_status_text) - 1);
    }
    portal_unlock();
}

static int g_http_fd = -1; // HTTP 服务器 socket 文件描述符
static int g_dns_fd = -1;  // DNS 劫持服务器 socket 文件描述符

static char g_switch_ssid[32] = {0};     // 待切换的目标 SSID
static char g_switch_password[64] = {0}; // 待切换的目标密码

#define SCAN_CACHE_MAX 16                                 // 扫描结果缓存最大条目数
static bsp_wifi_scan_item_t g_scan_cache[SCAN_CACHE_MAX]; // WiFi 扫描结果缓存
static uint32_t g_scan_cache_count = 0;                   // 当前缓存的扫描结果数量
static bool g_scan_cache_ready = false;                   // 扫描缓存是否已构建完成

#include "portal_html.h"

// URL 解码：%XX 转字符，+ 转空格
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

// 解析 HTTP POST 表单体，提取 ssid 和 password 字段
static bool parse_form_body(const char *body, char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    const char *p = body;
    bool got_ssid = false;
    ssid[0] = '\0';
    password[0] = '\0';

    while (p != NULL && *p != '\0') {
        const char *amp = strchr(p, '&');
        char pair[256] = {0};
        size_t pair_len = (amp != NULL) ? (size_t)(amp - p) : strlen(p);
        if (pair_len >= sizeof(pair))
            pair_len = sizeof(pair) - 1;
        (void)strncpy_s(pair, sizeof(pair), p, pair_len);

        char *eq = strchr(pair, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = pair;
            char *val = eq + 1;
            char decoded[128] = {0};
            url_decode(val, decoded, sizeof(decoded));

            if (strcmp(key, "ssid") == 0) {
                (void)strncpy_s(ssid, ssid_len, decoded, ssid_len - 1);
                got_ssid = true;
            } else if (strcmp(key, "password") == 0) {
                (void)strncpy_s(password, password_len, decoded, password_len - 1);
            }
        }
        p = (amp != NULL) ? amp + 1 : NULL;
    }
    return got_ssid && (ssid[0] != '\0');
}

// 扫描附近 WiFi 并更新缓存
static void refresh_scan_cache(void)
{
    bsp_wifi_scan_item_t tmp[SCAN_CACHE_MAX];
    uint32_t cnt = 0;
    int ret = bsp_wifi_scan_list(tmp, SCAN_CACHE_MAX, &cnt);
    portal_lock();
    g_scan_cache_ready = false;
    g_scan_cache_count = 0;
    if (ret == 0) {
        if (cnt > SCAN_CACHE_MAX)
            cnt = SCAN_CACHE_MAX;
        for (uint32_t i = 0; i < cnt; i++)
            g_scan_cache[i] = tmp[i];
        g_scan_cache_count = cnt;
        g_scan_cache_ready = true;
    }
    portal_unlock();
}

// 将扫描结果以 JSON 格式发送给 HTTP 客户端
static void send_scan_json(int client_fd, bsp_wifi_scan_item_t *items, uint32_t count, bool ok)
{
    size_t buf_size = 256 + count * 96;
    char *json = (char *)osal_kmalloc(buf_size, OSAL_GFP_ATOMIC);
    if (json == NULL) {
        http_send_response_and_close(client_fd, "HTTP/1.1 500\r\n\r\n");
        return;
    }

    int n =
        snprintf(json, buf_size,
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":%s,\"list\":[",
                 ok ? "true" : "false");
    for (uint32_t i = 0; i < count && n < (int)buf_size - 1; i++) {
        char esc[68] = {0};
        size_t ej = 0;
        for (size_t k = 0; items[i].ssid[k] != '\0' && ej + 2 < sizeof(esc); k++) {
            char c = items[i].ssid[k];
            if (c == '"' || c == '\\')
                esc[ej++] = '\\';
            esc[ej++] = c;
        }
        esc[ej] = '\0';

        int wrote =
            snprintf(json + n, buf_size - n, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"sec\":%u,\"ch\":%u}",
                     (i == 0) ? "" : ",", esc, items[i].rssi, (unsigned)items[i].security, (unsigned)items[i].channel);
        if (wrote > 0)
            n += wrote;
    }
    if (n >= 0 && (size_t)n < buf_size) {
        int wrote = snprintf(json + n, buf_size - n, "]}\r\n");
        if (wrote > 0)
            n += wrote;
    }

    (void)lwip_send(client_fd, json, (size_t)n, 0);
    lwip_close(client_fd);
    osal_kfree(json);
}

// 处理 /scan 请求，返回附近 WiFi 列表 JSON
static void handle_scan_request(int client_fd, const char *query)
{
    bool force_refresh = (query != NULL && strstr(query, "refresh=1") != NULL);
    if (force_refresh) {
        refresh_scan_cache();
    }

    bsp_wifi_scan_item_t snap[SCAN_CACHE_MAX];
    uint32_t snap_cnt = 0;
    bool ready;
    portal_lock();
    ready = g_scan_cache_ready;
    snap_cnt = g_scan_cache_count;
    for (uint32_t i = 0; i < snap_cnt; i++)
        snap[i] = g_scan_cache[i];
    portal_unlock();

    if (!ready || snap_cnt == 0) {
        refresh_scan_cache();
        portal_lock();
        ready = g_scan_cache_ready;
        snap_cnt = g_scan_cache_count;
        for (uint32_t i = 0; i < snap_cnt; i++)
            snap[i] = g_scan_cache[i];
        portal_unlock();
    }
    send_scan_json(client_fd, snap, snap_cnt, ready);
}

// 处理 /status 请求，返回当前 Portal 状态 JSON
static void handle_status_request(int client_fd)
{
    const char *status_str = "idle";
    char ip[16] = {0};

    switch (g_portal_status) {
        case PORTAL_STATUS_RUNNING:
            status_str = "running";
            break;
        case PORTAL_STATUS_CONFIG_RECEIVED:
        case PORTAL_STATUS_SWITCHING:
            status_str = "connecting";
            break;
        case PORTAL_STATUS_SUCCESS:
            status_str = "connected";
            break;
        case PORTAL_STATUS_FAILED:
            status_str = "failed";
            break;
        default:
            status_str = "idle";
            break;
    }

    if (g_portal_status == PORTAL_STATUS_SUCCESS) {
        (void)bsp_wifi_get_ip(ip, sizeof(ip));
    }

    char json[256];
    (void)snprintf(json, sizeof(json),
                   "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                   "close\r\n\r\n{\"status\":\"%s\",\"ip\":\"%s\"}\r\n",
                   status_str, ip);
    http_send_response_and_close(client_fd, json);
}

// 处理单个 HTTP 客户端连接，解析请求并路由到对应处理函数
static void handle_http_client(int client_fd)
{
    char buf[1536];
    int total = 0, n;

    struct timeval tv = {500 / 1000, (500 % 1000) * 1000};
    lwip_setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (total < (int)sizeof(buf) - 1) {
        n = lwip_recv(client_fd, buf + total, sizeof(buf) - 1 - total, 0);
        if (n <= 0)
            break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") != NULL)
            break;
    }

    if (total <= 0) {
        lwip_close(client_fd);
        return;
    }
    buf[total] = '\0';

    bool is_get = (strncmp(buf, "GET ", 4) == 0);
    bool is_post = (strncmp(buf, "POST ", 5) == 0);

    char path[32] = {0};
    const char *query = NULL;
    const char *path_start = strchr(buf, ' ');
    if (path_start != NULL) {
        path_start++;
        const char *path_end = strchr(path_start, ' ');
        if (path_end != NULL) {
            size_t len = (size_t)(path_end - path_start);
            if (len >= sizeof(path))
                len = sizeof(path) - 1;
            (void)strncpy_s(path, sizeof(path), path_start, len);
            char *q = strchr(path, '?');
            if (q != NULL) {
                *q = '\0';
                query = q + 1;
            }
        }
    }

    // 重定向检测：如果请求的目的主机不是 192.168.1.1，则强制 302 重定向到静态网关
    bool is_direct_ip = (strstr(buf, "Host: 192.168.1.1") != NULL);
    if (!is_direct_ip) {
        char redirect_resp[256];
        (void)snprintf(redirect_resp, sizeof(redirect_resp),
                       "HTTP/1.1 302 Found\r\nLocation: http://%s/%s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                       PORTAL_STATIC_IP, (g_portal_status == PORTAL_STATUS_FAILED) ? "?fail=1" : "");
        http_send_response_and_close(client_fd, redirect_resp);
        return;
    }

    if (http_channel_handle(client_fd, is_get, path, query))
        return;
    if (is_get && strcmp(path, "/status") == 0) {
        handle_status_request(client_fd);
        return;
    }

    // 强制探测 URL 拦截
    if (is_get &&
        (strcmp(path, "/generate_204") == 0 || strcmp(path, "/hotspot-detect.html") == 0 ||
         strcmp(path, "/ncsi.txt") == 0 || strcmp(path, "/success.txt") == 0 || strcmp(path, "/redirect") == 0)) {
        http_send_html_response(client_fd, s_html_page);
        return;
    }

    if (is_get && strcmp(path, "/scan") == 0) {
        handle_scan_request(client_fd, query);
        return;
    }
    if (is_get && strcmp(path, "/result") == 0) {
        if (g_portal_status == PORTAL_STATUS_FAILED) {
            http_send_html_response(client_fd, s_html_fail_result);
        } else {
            char red[128];
            snprintf(red, sizeof(red), "HTTP/1.1 302 Found\r\nLocation: http://%s/\r\n\r\n", PORTAL_STATIC_IP);
            http_send_response_and_close(client_fd, red);
        }
        return;
    }

    if (is_get && strcmp(path, "/") == 0) {
        http_send_html_response(client_fd, s_html_page);
        return;
    }

    if (is_post && strcmp(path, "/config") == 0) {
        if (g_portal_status == PORTAL_STATUS_CONFIG_RECEIVED || g_portal_status == PORTAL_STATUS_SWITCHING) {
            http_send_html_response(client_fd, s_html_busy);
            return;
        }

        int content_len = 0;
        const char *cl = strstr(buf, "Content-Length:");
        if (cl != NULL)
            sscanf(cl + 15, "%d", &content_len);

        char *body_start = strstr(buf, "\r\n\r\n");
        int body_received = 0;
        char body[512] = {0};

        if (body_start != NULL) {
            body_start += 4;
            body_received = total - (int)(body_start - buf);
            if (body_received > 0) {
                int copy_len = body_received;
                if (copy_len >= (int)sizeof(body))
                    copy_len = (int)sizeof(body) - 1;
                memcpy(body, body_start, copy_len);
                body[copy_len] = '\0';
            }
        }

        if (content_len > body_received && content_len < (int)sizeof(body)) {
            int need = content_len - body_received;
            n = lwip_recv(client_fd, body + body_received, need, 0);
            if (n > 0) {
                body_received += n;
                body[body_received] = '\0';
            }
        }

        char ssid[32] = {0}, password[64] = {0};
        if (parse_form_body(body, ssid, sizeof(ssid), password, sizeof(password))) {
            portal_set_status(PORTAL_STATUS_CONFIG_RECEIVED, "配网中...");
            storage_service_save_wifi_config(ssid, password);

            portal_lock();
            (void)strncpy_s(g_switch_ssid, sizeof(g_switch_ssid), ssid, sizeof(g_switch_ssid) - 1);
            (void)strncpy_s(g_switch_password, sizeof(g_switch_password), password, sizeof(g_switch_password) - 1);
            portal_unlock();

            portal_set_status(PORTAL_STATUS_SWITCHING, "切换STA");
            wifi_mgr_connect_ap_from_portal(g_switch_ssid, g_switch_password);
            http_send_html_response(client_fd, s_html_submitted);
            return;
        } else {
            http_send_html_response(client_fd, s_html_submitted);
            return;
        }
    }

    char redirect_root[256];
    (void)snprintf(redirect_root, sizeof(redirect_root),
                   "HTTP/1.1 302 Found\r\nLocation: http://%s/%s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                   PORTAL_STATIC_IP, (g_portal_status == PORTAL_STATUS_FAILED) ? "?fail=1" : "");
    http_send_response_and_close(client_fd, redirect_root);
}

// 创建并启动 HTTP 服务器（监听 80 端口）
static int http_server_start(void)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int opt = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(80);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lwip_close(fd);
        return -1;
    }
    if (lwip_listen(fd, 5) < 0) {
        lwip_close(fd);
        return -1;
    }
    printf("[Portal] HTTP 已启动:80\r\n");
    return fd;
}

// 关闭 HTTP 服务器 socket
static void http_server_stop(int fd)
{
    if (fd >= 0)
        lwip_close(fd);
}

// 创建并启动 DNS 服务器（监听 53 端口，用于 Captive Portal 劫持）
static int dns_server_start(void)
{
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    int opt = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(53);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lwip_close(fd);
        return -1;
    }
    struct timeval tv = {0, 100000};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    printf("[Portal] DNS 已启动:53\r\n");
    return fd;
}

// 关闭 DNS 服务器 socket
static void dns_server_stop(int fd)
{
    if (fd >= 0)
        lwip_close(fd);
}

// 处理 DNS 查询，将所有 A 记录响应重定向到 AP 静态 IP
static void dns_server_handle(int fd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    unsigned char buf[512];
    unsigned char resp[512];

    int n = lwip_recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &addr_len);
    if (n < 12)
        return;
    if ((buf[2] & 0x80) != 0)
        return;

    int pos = 12;
    while (pos < n && buf[pos] != 0) {
        if ((buf[pos] & 0xC0) == 0xC0) {
            pos += 2;
            goto copy_question;
        }
        pos += 1 + buf[pos];
    }
    pos++;
copy_question:
    pos += 4;
    int q_len = pos - 12;
    if (pos > n || 12 + q_len + 16 > (int)sizeof(resp))
        return;

    memcpy(resp, buf, 12);
    resp[2] = 0x81;
    resp[3] = 0x80;
    resp[6] = 0x00;
    resp[7] = 0x01;
    resp[8] = 0x00;
    resp[9] = 0x00;
    resp[10] = 0x00;
    resp[11] = 0x00;

    memcpy(resp + 12, buf + 12, q_len);
    int resp_len = 12 + q_len;

    // 直接对任意 A 记录 DNS 查询返回 192.168.1.1
    unsigned int ap_ip = inet_addr(PORTAL_STATIC_IP);
    static const uint8_t a_rr_prefix[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2C, 0x00, 0x04};
    memcpy(resp + resp_len, a_rr_prefix, sizeof(a_rr_prefix));
    resp_len += sizeof(a_rr_prefix);
    memcpy(resp + resp_len, &ap_ip, 4);
    resp_len += 4;

    (void)lwip_sendto(fd, resp, resp_len, 0, (struct sockaddr *)&client_addr, addr_len);
}

// Portal 主任务：检测 AP 模式，驱动 HTTP/DNS 服务器的启停与事件循环
static int captive_portal_task(void *arg)
{
    (void)arg;
    bool server_running = false;

    while (!g_task_should_exit) {
        bsp_wifi_mode_t mode = bsp_wifi_get_mode();

        if (mode == BSP_WIFI_MODE_AP) {
            if (!server_running) {
                printf("[Portal] 检测到 AP 模式，启动 HTTP/DNS\r\n");
                g_http_fd = http_server_start();
                if (g_http_fd >= 0) {
                    g_dns_fd = dns_server_start();
                    server_running = true;
                    portal_set_status(PORTAL_STATUS_RUNNING, "等待配网");
                    refresh_scan_cache();
                } else {
                    // 启动失败时延迟重试，避免死循环打爆 CPU
                    osal_msleep(500);
                }
            }

            if (server_running && g_http_fd >= 0) {
                fd_set readset;
                FD_ZERO(&readset);
                FD_SET(g_http_fd, &readset);
                int max_fd = g_http_fd;

                if (g_dns_fd >= 0) {
                    FD_SET(g_dns_fd, &readset);
                    if (g_dns_fd > max_fd)
                        max_fd = g_dns_fd;
                }

                struct timeval tv = {0, 500000};
                int ret = lwip_select(max_fd + 1, &readset, NULL, NULL, &tv);

                if (ret > 0) {
                    if (g_dns_fd >= 0 && FD_ISSET(g_dns_fd, &readset)) {
                        dns_server_handle(g_dns_fd);
                    }
                    if (FD_ISSET(g_http_fd, &readset)) {
                        struct sockaddr_in client_addr;
                        socklen_t addr_len = sizeof(client_addr);
                        int client_fd = lwip_accept(g_http_fd, (struct sockaddr *)&client_addr, &addr_len);
                        if (client_fd >= 0) {
                            handle_http_client(client_fd);
                        }
                    }
                }
            }
        } else {
            if (server_running) {
                http_server_stop(g_http_fd);
                dns_server_stop(g_dns_fd);
                g_http_fd = -1;
                g_dns_fd = -1;
                server_running = false;
                portal_set_status(PORTAL_STATUS_IDLE, NULL);
            }
            // 超时 500ms：事件正常立即唤醒，事件丢失时兜底检测 AP 模式
            unsigned int wait_mask = 0x01 | 0x04;
            (void)osal_event_read(&g_portal_event, wait_mask, 500, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        }
    }

    if (server_running) {
        http_server_stop(g_http_fd);
        dns_server_stop(g_dns_fd);
    }
    return 0;
}

// WiFi 事件订阅回调（运行在 wifi_mgr 任务上下文）：只转发给本模块的事件/状态接口
static void portal_wifi_event_cb(bsp_wifi_event_t event, const char *ip)
{
    (void)ip;
    switch (event) {
        case BSP_WIFI_EVENT_AP_READY:
            captive_portal_service_notify_ap_ready();
            break;
        case BSP_WIFI_EVENT_AP_STOPPED:
            captive_portal_service_notify_ap_stopped();
            break;
        case BSP_WIFI_EVENT_STA_FAIL:
            captive_portal_service_notify_sta_fail();
            break;
        default:
            break;
    }
}

// 初始化 Portal 服务：创建互斥锁、事件和主任务
void captive_portal_service_init(void)
{
    if (g_portal_task != NULL)
        return;
    if (!g_portal_lock_inited) {
        if (osal_mutex_init(&g_portal_lock) == OSAL_SUCCESS)
            g_portal_lock_inited = true;
    }
    if (!g_portal_event_inited) {
        if (osal_event_init(&g_portal_event) == OSAL_SUCCESS)
            g_portal_event_inited = true;
    }

    (void)wifi_mgr_subscribe(portal_wifi_event_cb);

    g_task_should_exit = false;
    g_portal_task = car_task_create_locked("portal_task", (osal_kthread_handler)captive_portal_task, NULL, 8192, 23);
}

// 获取 AP 模式的静态 IP 地址字符串
const char *captive_portal_service_get_ap_ip(void)
{
    return PORTAL_STATIC_IP;
}

// 通知 Portal 任务 AP 模式已就绪
void captive_portal_service_notify_ap_ready(void)
{
    if (g_portal_event_inited)
        (void)osal_event_write(&g_portal_event, 0x01);
}

// 通知 Portal 任务 AP 模式已停止
void captive_portal_service_notify_ap_stopped(void)
{
    if (g_portal_event_inited)
        (void)osal_event_write(&g_portal_event, 0x02);
}

// 通知 Portal 任务 STA 连接失败，提示用户重新配网
void captive_portal_service_notify_sta_fail(void)
{
    portal_set_status(PORTAL_STATUS_FAILED, "连接失败，请重新配网");
}
