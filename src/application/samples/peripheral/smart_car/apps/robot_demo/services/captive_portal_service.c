/**
 * @file        captive_portal_service.c
 * @brief       AP 配网服务（Captive Portal）实现
 * @details     在 AP 模式下启动轻量级 HTTP 服务器 + DNS 劫持服务器。
 *              用户连接小车热点后自动弹出（或访问任意网址跳转到）Web 配网页面。
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
#include "captive_portal_control.h"

/* ---------- 配置常量 ---------- */
#define CAPTIVE_PORTAL_HTTP_PORT     80
#define CAPTIVE_PORTAL_DNS_PORT      53
#define CAPTIVE_PORTAL_STACK_SIZE    8192
#define CAPTIVE_PORTAL_TASK_PRIO     23
#define CAPTIVE_HTTP_RECV_TIMEOUT_MS 500
#define CAPTIVE_HTTP_BUF_SIZE        1536
#define CAPTIVE_HTTP_MAX_BODY        512
#define CAPTIVE_DNS_BUF_SIZE         512

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

/* DNS 与 HTTP 套接字 */
static int       g_http_fd = -1;
static int       g_dns_fd  = -1;

/* 后台 WiFi 切换任务 */
static osal_task *g_switch_task = NULL;
static char      g_switch_ssid[32] = {0};
static char      g_switch_password[64] = {0};

/* WiFi 扫描缓存 */
#define SCAN_CACHE_MAX 16
static bsp_wifi_scan_item_t g_scan_cache[SCAN_CACHE_MAX];
static uint32_t g_scan_cache_count = 0;
static bool g_scan_cache_ready = false;

/* ---------- 内嵌配网页面 ---------- */
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
    "<p class=\"sub\">连接你的WiFi，让小车接入局域网</p>"
    "<form method=\"POST\" action=\"/config\">"
    "<div class=\"field\">"
    "<label>WiFi 名称 (SSID)</label>"
    "<input type=\"text\" id=\"ssid\" name=\"ssid\" placeholder=\"请输入WiFi名称\" required maxlength=31>"
    "<select id=\"ssid_sel\" onchange=\"pickSsid()\" style=\"width:100%;margin-top:8px;padding:10px;border:1px solid #ddd;border-radius:10px;font-size:14px;background:#fafafa\">"
    "<option value=\"\">-- 附近 WiFi --</option>"
    "</select>"
    "<button type=\"button\" onclick=\"scanWifi(true)\" style=\"margin-top:8px;background:#e5e5ea;color:#333;font-size:13px;padding:8px\">刷新 WiFi 列表</button>"
    "<p id=\"scan_tip\" style=\"font-size:12px;color:#999;margin-top:4px\">正在加载附近 WiFi...</p>"
    "</div>"
    "<div class=\"field\">"
    "<label>WiFi 密码</label>"
    "<input type=\"password\" name=\"password\" placeholder=\"请输入WiFi密码\" maxlength=63>"
    "</div>"
    "<button type=\"submit\">保存并连接</button>"
    "</form>"
    "<div style=\"margin-top:18px;text-align:center\">"
    "<a href=\"/control\" style=\"display:inline-block;padding:10px 20px;background:#34c759;color:#fff;text-decoration:none;border-radius:8px;font-size:15px;font-weight:600\">点击控制小车</a>"
    "<p style=\"margin-top:8px;font-size:12px;color:#999\">无需配网也可直接遥控</p>"
    "</div>"
    "<p class=\"tip\">提示：密码为空表示连接开放网络<br>配网成功后页面将自动跳转</p>"
    "</div>"
    "<script>"
    "function pickSsid(){var s=document.getElementById('ssid_sel');if(s.value){document.getElementById('ssid').value=s.value;}}"
    "function scanWifi(refresh){"
    "var t=document.getElementById('scan_tip');t.textContent=refresh?'刷新中，请等待...':'加载中...';"
    "var x=new XMLHttpRequest();x.open('GET',refresh?'/scan?refresh=1':'/scan',true);x.timeout=8000;"
    "x.onreadystatechange=function(){if(x.readyState==4){"
    "if(x.status==200){try{var d=JSON.parse(x.responseText);var s=document.getElementById('ssid_sel');"
    "s.innerHTML='<option value=\"\">-- 选择 SSID --</option>';"
    "d.list.forEach(function(it){var o=document.createElement('option');o.value=it.ssid;"
    "o.textContent=it.ssid+' ('+it.rssi+'dBm'+(it.sec>0?' 加密':' 开放')+')';s.appendChild(o);});"
    "t.textContent='共发现 '+d.list.length+' 个网络';}catch(e){t.textContent='解析失败';}}"
    "else{t.textContent='加载失败';}}};"
    "x.ontimeout=function(){t.textContent='加载超时';};"
    "x.send();}"
    "window.onload=function(){scanWifi(false);};"
    "</script>"
    "</body></html>";

static const char s_html_success[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>配网中</title>"
    "<style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#f2f3f5;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
    ".card{background:#fff;border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,.08);padding:32px;width:100%;max-width:320px;text-align:center}"
    "h1{font-size:22px;color:#007aff;margin-bottom:8px}"
    "p{color:#555;font-size:15px;line-height:1.6}"
    ".spin{display:inline-block;width:24px;height:24px;border:3px solid #ddd;border-top-color:#007aff;border-radius:50%;animation:s 1s linear infinite;margin-bottom:12px}"
    "@keyframes s{to{transform:rotate(360deg)}}"
    "a{display:inline-block;margin-top:16px;padding:10px 20px;background:#007aff;color:#fff;text-decoration:none;border-radius:8px;font-size:15px}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<div class=\"spin\"></div>"
    "<h1 id=\"t\">正在连接WiFi...</h1>"
    "<p id=\"m\">请稍候，正在尝试连接网络</p>"
    "</div>"
    "<script>"
    "function c(){"
    "var x=new XMLHttpRequest();"
    "x.open('GET','/status',true);"
    "x.onreadystatechange=function(){"
    "if(x.readyState==4){"
    "if(x.status==200){"
    "var d=JSON.parse(x.responseText);"
    "if(d.status=='connected'){"
    "document.getElementById('t').innerHTML='配网成功!';"
    "document.getElementById('t').style.color='#34c759';"
    "document.getElementById('m').innerHTML='IP: '+d.ip+'<br>热点即将关闭';"
    "}else if(d.status=='failed'){"
    "document.getElementById('t').innerHTML='配网失败';"
    "document.getElementById('t').style.color='#ff3b30';"
    "document.getElementById('m').innerHTML='无法连接到WiFi<br><a href=\"/\">返回重试</a>';"
    "}else{setTimeout(c,2000);}"
    "}else{setTimeout(c,2000);}"
    "}"
    "};"
    "x.send();"
    "}"
    "c();"
    "</script>"
    "</body></html>";

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

/* 302 重定向响应（动态 IP，在 handle_http_client 中生成） */

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
 * @brief 执行一次 WiFi 扫描并更新缓存
 */
static void refresh_scan_cache(void)
{
    g_scan_cache_ready = false;
    g_scan_cache_count = 0;
    if (bsp_wifi_scan_list(g_scan_cache, SCAN_CACHE_MAX, &g_scan_cache_count) == 0) {
        g_scan_cache_ready = true;
        printf("[Portal] WiFi 扫描缓存更新: %u 条\r\n", g_scan_cache_count);
    } else {
        printf("[Portal] WiFi 扫描失败\r\n");
    }
}

/**
 * @brief 将扫描结果列表序列化为 JSON 并发送
 */
static void send_scan_json(int client_fd, bsp_wifi_scan_item_t* items,
                           uint32_t count, bool ok)
{
    size_t buf_size = 256 + count * 96;
    char* json = (char*)osal_kmalloc(buf_size, OSAL_GFP_ATOMIC);
    if (json == NULL) {
        send_response_and_close(client_fd, "HTTP/1.1 500\r\n\r\n");
        return;
    }

    int n = snprintf(json, buf_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n\r\n"
        "{\"ok\":%s,\"list\":[", ok ? "true" : "false");
    if (n < 0 || (size_t)n >= buf_size) n = (int)buf_size - 1;

    for (uint32_t i = 0; i < count && n < (int)buf_size - 1; i++) {
        char esc[68] = {0};
        size_t ej = 0;
        for (size_t k = 0; items[i].ssid[k] != '\0' && ej + 2 < sizeof(esc); k++) {
            char c = items[i].ssid[k];
            if (c == '"' || c == '\\') esc[ej++] = '\\';
            esc[ej++] = c;
        }
        esc[ej] = '\0';

        int wrote = snprintf(json + n, buf_size - n,
                      "%s{\"ssid\":\"%s\",\"rssi\":%d,\"sec\":%u,\"ch\":%u}",
                      (i == 0) ? "" : ",", esc, items[i].rssi,
                      (unsigned)items[i].security, (unsigned)items[i].channel);
        if (wrote > 0) n += wrote;
    }
    if (n >= 0 && (size_t)n < buf_size) {
        int wrote = snprintf(json + n, buf_size - n, "]}\r\n");
        if (wrote > 0) n += wrote;
    }

    (void)lwip_send(client_fd, json, (size_t)n, 0);
    lwip_close(client_fd);
    osal_kfree(json);
}

/**
 * @brief 处理 /scan 请求，扫描附近 WiFi 并以 JSON 返回
 * @param client_fd 客户端 socket
 * @param query 查询字符串，支持 ?refresh=1 强制刷新
 * @note 默认返回缓存结果；refresh=1 时实时扫描并更新缓存
 */
static void handle_scan_request(int client_fd, const char* query)
{
    bool force_refresh = (query != NULL && strstr(query, "refresh=1") != NULL);

    if (force_refresh) {
        const uint32_t MAX_ITEMS = 16;
        bsp_wifi_scan_item_t* items =
            (bsp_wifi_scan_item_t*)osal_kmalloc(sizeof(bsp_wifi_scan_item_t) * MAX_ITEMS, OSAL_GFP_ATOMIC);
        if (items == NULL) {
            send_response_and_close(client_fd, "HTTP/1.1 500\r\n\r\n");
            return;
        }
        uint32_t count = 0;
        int ret = bsp_wifi_scan_list(items, MAX_ITEMS, &count);
        if (ret == 0) {
            /* 更新缓存 */
            g_scan_cache_count = (count > SCAN_CACHE_MAX) ? SCAN_CACHE_MAX : count;
            for (uint32_t i = 0; i < g_scan_cache_count; i++) {
                g_scan_cache[i] = items[i];
            }
            g_scan_cache_ready = true;
        }
        send_scan_json(client_fd, items, count, ret == 0);
        osal_kfree(items);
        printf("[Portal] /scan?refresh=1 返回 %u 条结果\r\n", count);
        return;
    }

    /* 默认返回缓存 */
    if (g_scan_cache_ready && g_scan_cache_count > 0) {
        send_scan_json(client_fd, g_scan_cache, g_scan_cache_count, true);
        printf("[Portal] /scan 返回缓存 %u 条结果\r\n", g_scan_cache_count);
    } else {
        /* 缓存为空， fallback 到实时扫描 */
        refresh_scan_cache();
        send_scan_json(client_fd, g_scan_cache, g_scan_cache_count, g_scan_cache_ready);
        printf("[Portal] /scan 缓存未命中，实时扫描返回 %u 条\r\n", g_scan_cache_count);
    }
}

/**
 * @brief 处理 /status 请求，返回 JSON 状态
 */
static void handle_status_request(int client_fd)
{
    const char *status_str = "idle";
    char ip[BUF_IP] = {0};

    switch (g_portal_status) {
        case PORTAL_STATUS_RUNNING:      status_str = "running";    break;
        case PORTAL_STATUS_CONFIG_RECEIVED:
        case PORTAL_STATUS_SWITCHING:    status_str = "connecting"; break;
        case PORTAL_STATUS_SUCCESS:      status_str = "connected";  break;
        case PORTAL_STATUS_FAILED:       status_str = "failed";     break;
        default:                         status_str = "idle";       break;
    }

    if (g_portal_status == PORTAL_STATUS_SUCCESS) {
        (void)bsp_wifi_get_ip(ip, sizeof(ip));
    }

    char json[256];
    (void)snprintf(json, sizeof(json),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n\r\n"
        "{\"status\":\"%s\",\"ip\":\"%s\"}\r\n",
        status_str, ip);

    send_response_and_close(client_fd, json);
}

/**
 * @brief 后台 WiFi 切换任务
 */
static int wifi_switch_task(void *arg)
{
    (void)arg;

    g_portal_status = PORTAL_STATUS_SWITCHING;
    strncpy(g_status_text, "切换STA", sizeof(g_status_text));

    printf("[Portal] 正在从 AP 切换到 STA 模式...\r\n");
    if (bsp_wifi_switch_from_ap_to_sta(g_switch_ssid, g_switch_password) == 0) {
        g_portal_status = PORTAL_STATUS_SUCCESS;
        strncpy(g_status_text, "配网成功", sizeof(g_status_text));
        printf("[Portal] 切换到 STA 成功\r\n");
    } else {
        g_portal_status = PORTAL_STATUS_FAILED;
        strncpy(g_status_text, "配网失败", sizeof(g_status_text));
        printf("[Portal] 切换到 STA 失败\r\n");
    }

    g_switch_task = NULL;
    return 0;
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

    /* 解析路径（含 query string 分离） */
    char path[32] = {0};
    const char *query = NULL;
    const char *path_start = strchr(buf, ' ');
    if (path_start != NULL) {
        path_start++;
        const char *path_end = strchr(path_start, ' ');
        if (path_end != NULL) {
            size_t len = (size_t)(path_end - path_start);
            if (len >= sizeof(path)) len = sizeof(path) - 1;
            strncpy(path, path_start, len);
            path[len] = '\0';

            /* 分离 query string */
            char *q = strchr(path, '?');
            if (q != NULL) {
                *q = '\0';
                query = q + 1;
            }
        }
    }

    /* ========== 核心修复：检查 Host ========== */
    /* 判断浏览器请求的域名是不是小车自己的 IP */
    bool is_direct_ip = false;
    char target_host[64];
    (void)snprintf(target_host, sizeof(target_host), "Host: %s", g_ap_ip_str);
    if (strstr(buf, target_host) != NULL) {
        is_direct_ip = true;
    }

    /* 如果是通过 DNS 劫持过来的（Host 不是小车 IP） */
    if (!is_direct_ip) {
        /* 302 强制跳转到小车真实 IP */
        char redirect_resp[256];
        (void)snprintf(redirect_resp, sizeof(redirect_resp),
            "HTTP/1.1 302 Found\r\n"
            "Location: http://%s/\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n",
            g_ap_ip_str);
        send_response_and_close(client_fd, redirect_resp);
        return;
    }
    /* ======================================== */

    /* 控制相关路径 (/control, /api/...) */
    if (captive_portal_control_handle(client_fd, is_get, path, query)) {
        return;
    }

    /* GET /status -> 返回 JSON */
    if (is_get && strcmp(path, "/status") == 0) {
        handle_status_request(client_fd);
        return;
    }

    /* GET /scan -> 扫描附近 WiFi 并返回 JSON 列表 */
    if (is_get && strcmp(path, "/scan") == 0) {
        handle_scan_request(client_fd, query);
        return;
    }

    /* GET / -> 返回真正的配网页面 */
    if (is_get && strcmp(path, "/") == 0) {
        send_response_and_close(client_fd, s_html_page);
        return;
    }

    /* POST /config 处理配网 */
    if (is_post && strcmp(path, "/config") == 0) {
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

            /* 保存配置 */
            storage_service_save_wifi_config(ssid, password);

            /* 保存到全局变量供后台任务使用 */
            strncpy(g_switch_ssid, ssid, sizeof(g_switch_ssid));
            strncpy(g_switch_password, password, sizeof(g_switch_password));

            /* 先返回成功页面 */
            send_response_and_close(client_fd, s_html_success);

            /* 启动后台 WiFi 切换任务 */
            if (g_switch_task == NULL) {
                osal_kthread_lock();
                g_switch_task = osal_kthread_create(
                    (osal_kthread_handler)wifi_switch_task, NULL,
                    "wifi_switch", 4096);
                if (g_switch_task != NULL) {
                    osal_kthread_set_priority(g_switch_task, CAPTIVE_PORTAL_TASK_PRIO);
                }
                osal_kthread_unlock();
            }
            return;
        } else {
            send_response_and_close(client_fd, s_html_fail);
            return;
        }
    }

    /* 控制相关路径 (/control, /api/...) */
    if (captive_portal_control_handle(client_fd, is_get, path, query)) {
        return;
    }

    /* 访问了 IP 的其他不存在路径，也跳回根目录 */
    char redirect_root[256];
    (void)snprintf(redirect_root, sizeof(redirect_root),
        "HTTP/1.1 302 Found\r\n"
        "Location: http://%s/\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n",
        g_ap_ip_str);
    send_response_and_close(client_fd, redirect_root);
}

/**
 * @brief 启动 HTTP 监听 socket
 */
static int http_server_start(void)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[Portal] HTTP socket 创建失败\r\n");
        return -1;
    }

    int opt = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(CAPTIVE_PORTAL_HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[Portal] HTTP bind 失败\r\n");
        lwip_close(fd);
        return -1;
    }

    if (lwip_listen(fd, 5) < 0) {
        printf("[Portal] HTTP listen 失败\r\n");
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
 * @brief 启动 DNS 劫持服务器（UDP 端口 53）
 * @note 将所有 A 记录查询重定向到 192.168.1.1
 */
static int dns_server_start(void)
{
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        printf("[Portal] DNS socket 创建失败\r\n");
        return -1;
    }

    int opt = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(CAPTIVE_PORTAL_DNS_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[Portal] DNS bind 失败\r\n");
        lwip_close(fd);
        return -1;
    }

    /* 设置接收超时，使轮询可以定期检查退出标志 */
    struct timeval tv = {0, 100000}; /* 100ms */
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("[Portal] DNS 服务器已启动: port=%d\r\n", CAPTIVE_PORTAL_DNS_PORT);
    return fd;
}

/**
 * @brief 关闭 DNS 服务器
 */
static void dns_server_stop(int fd)
{
    if (fd >= 0) {
        lwip_close(fd);
        printf("[Portal] DNS 服务器已停止\r\n");
    }
}

/**
 * @brief 处理单个 DNS 查询
 * @note 解析 DNS 请求，对所有 A 记录查询返回 192.168.1.1
 */
/**
 * @brief 从 DNS 查询包中提取域名（调试用）
 */
static void dns_extract_name(const unsigned char *buf, int pos, char *out, size_t out_len)
{
    size_t j = 0;
    while (buf[pos] != 0 && j + 1 < out_len) {
        int len = buf[pos];
        if ((len & 0xC0) == 0xC0) break; /* 压缩指针，停止 */
        pos++;
        for (int i = 0; i < len && j + 1 < out_len; i++) {
            out[j++] = (char)buf[pos++];
        }
        if (buf[pos] != 0 && j + 1 < out_len) out[j++] = '.';
    }
    out[j] = '\0';
}

static void dns_server_handle(int fd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    unsigned char buf[CAPTIVE_DNS_BUF_SIZE];
    unsigned char resp[CAPTIVE_DNS_BUF_SIZE];

    int n = lwip_recvfrom(fd, buf, sizeof(buf), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
    if (n < 12) return;

    /* 解析 DNS 头部 */
    unsigned int flags = ((unsigned int)buf[2] << 8) | buf[3];
    unsigned int questions = ((unsigned int)buf[4] << 8) | buf[5];

    /* 只处理标准查询（QR=0），且至少有一个问题 */
    if ((flags & 0x8000) != 0) return;
    if (questions == 0) return;

    /* 提取域名用于调试 */
    char qname[128] = {0};
    dns_extract_name(buf, 12, qname, sizeof(qname));

    /* 构建响应头部：复制事务ID和Questions，设置响应标志 */
    memcpy(resp, buf, 2);                   /* Transaction ID */
    resp[2] = 0x81; resp[3] = 0x80;         /* Flags: Standard response, No error */
    memcpy(resp + 4, buf + 4, 2);           /* Questions */
    resp[6] = 0x00; resp[7] = 0x01;         /* Answer RRs: 1 */
    resp[8] = 0x00; resp[9] = 0x00;         /* Authority RRs: 0 */
    resp[10] = 0x00; resp[11] = 0x00;       /* Additional RRs: 0 */
    int resp_len = 12;

    /* 复制问题部分 */
    int pos = 12;
    while (pos < n && buf[pos] != 0) {
        if ((buf[pos] & 0xC0) == 0xC0) {
            /* 查询中不应出现压缩指针，直接丢弃 */
            return;
        }
        pos += 1 + buf[pos];
    }
    if (pos >= n || buf[pos] != 0) return;
    pos++; /* 跳过域名结束符 \0 */
    if (pos + 4 > n) return; /* 需要 QTYPE + QCLASS */

    int q_len = pos + 4 - 12;
    if (resp_len + q_len > (int)sizeof(resp)) return;
    memcpy(resp + 12, buf + 12, q_len);
    resp_len += q_len;

    /* 解析当前 AP IP */
    unsigned int ap_ip = inet_addr(g_ap_ip_str);
    if (ap_ip == 0 || ap_ip == (unsigned int)(-1)) {
        /* 回退到默认 IP */
        ap_ip = inet_addr("192.168.1.1");
    }

    /* 添加 A 记录回答 */
    if (resp_len + 16 > (int)sizeof(resp)) return;

    resp[resp_len++] = 0xC0; resp[resp_len++] = 0x0C; /* Name: 指针指向问题域名（偏移 12） */
    resp[resp_len++] = 0x00; resp[resp_len++] = 0x01; /* Type: A */
    resp[resp_len++] = 0x00; resp[resp_len++] = 0x01; /* Class: IN */
    resp[resp_len++] = 0x00; resp[resp_len++] = 0x00; /* TTL: 300 秒 */
    resp[resp_len++] = 0x01; resp[resp_len++] = 0x2C;
    resp[resp_len++] = 0x00; resp[resp_len++] = 0x04; /* Data length: 4 */
    memcpy(&resp[resp_len], &ap_ip, 4);
    resp_len += 4;

    (void)lwip_sendto(fd, resp, resp_len, 0,
                      (struct sockaddr *)&client_addr, addr_len);

    printf("[Portal] DNS 劫持: %s -> %s\r\n", qname, g_ap_ip_str);
}

/**
 * @brief 配网服务主任务
 * @note 使用 lwip_select 同时监听 HTTP (TCP/80) 和 DNS (UDP/53) 端口，
 *       避免串行轮询导致的 DNS 查询漏接问题。
 */
static int captive_portal_task(void *arg)
{
    (void)arg;
    bool server_running = false;

    while (!g_task_should_exit) {
        bsp_wifi_mode_t mode = bsp_wifi_get_mode();

        if (mode == BSP_WIFI_MODE_AP) {
            /* AP 模式下启动服务器（HTTP 必须先启动） */
            if (!server_running) {
                g_http_fd = http_server_start();
                if (g_http_fd >= 0) {
                    g_dns_fd = dns_server_start();
                    server_running = true;
                    g_portal_status = PORTAL_STATUS_RUNNING;
                    strncpy(g_status_text, "等待配网", sizeof(g_status_text));

                    /* 更新 IP 显示 */
                    (void)bsp_wifi_get_ip(g_ap_ip_str, sizeof(g_ap_ip_str));
                    printf("[Portal] AP IP: %s, 请用手机连接 %s 后访问 http://%s/\r\n",
                           g_ap_ip_str, BSP_WIFI_AP_SSID, g_ap_ip_str);

                    /* AP 启动后自动扫描一次 WiFi 并缓存 */
                    refresh_scan_cache();
                }
            }

            if (server_running && g_http_fd >= 0) {
                fd_set readset;
                FD_ZERO(&readset);
                FD_SET(g_http_fd, &readset);
                int max_fd = g_http_fd;

                if (g_dns_fd >= 0) {
                    FD_SET(g_dns_fd, &readset);
                    if (g_dns_fd > max_fd) max_fd = g_dns_fd;
                }

                /* select 500ms 超时，既能及时响应又能定期检查退出标志 */
                struct timeval tv = {0, 500000};
                int ret = lwip_select(max_fd + 1, &readset, NULL, NULL, &tv);

                if (ret > 0) {
                    /* 优先处理 DNS 查询（低延迟） */
                    if (g_dns_fd >= 0 && FD_ISSET(g_dns_fd, &readset)) {
                        dns_server_handle(g_dns_fd);
                    }
                    /* 处理 HTTP 连接 */
                    if (FD_ISSET(g_http_fd, &readset)) {
                        struct sockaddr_in client_addr;
                        socklen_t addr_len = sizeof(client_addr);
                        int client_fd = lwip_accept(g_http_fd,
                                                    (struct sockaddr *)&client_addr, &addr_len);
                        if (client_fd >= 0) {
                            printf("[Portal] 客户端连接: %s\r\n",
                                   inet_ntoa(client_addr.sin_addr));
                            handle_http_client(client_fd);
                        }
                    }
                }
            }
        } else {
            /* 非 AP 模式关闭所有服务器 */
            if (server_running) {
                http_server_stop(g_http_fd);
                dns_server_stop(g_dns_fd);
                g_http_fd = -1;
                g_dns_fd  = -1;
                server_running = false;
                g_portal_status = PORTAL_STATUS_IDLE;
            }
            osal_msleep(500);
        }
    }

    if (server_running) {
        http_server_stop(g_http_fd);
        dns_server_stop(g_dns_fd);
    }
    g_portal_task = NULL;
    return 0;
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
        printf("[Portal] Captive Portal 配网服务已初始化\r\n");
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
        (void)bsp_wifi_get_ip(g_ap_ip_str, sizeof(g_ap_ip_str));
    }
    return g_ap_ip_str;
}

const char* captive_portal_service_get_status_text(void)
{
    return g_status_text;
}

