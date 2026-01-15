#include "http_ctrl_service.h"

#include "../core/robot_mgr.h"
#include "net_service.h"

#include "securec.h"
#include "soc_osal.h"

#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_CTRL_LISTEN_PORT 8080
#define HTTP_CTRL_STACK_SIZE 4096
#define HTTP_CTRL_TASK_PRIORITY 23

static void *http_ctrl_task(const char *arg);

static osal_task *g_http_task_handle = NULL;
static bool g_http_started = false;

static const char g_index_html[] =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\"/>\n"
    "  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>\n"
    "  <title>Smart Car Control</title>\n"
    "  <link rel=\"stylesheet\" href=\"/style.css\"/>\n"
    "</head>\n"
    "<body>\n"
    "  <div class=\"wrap\">\n"
    "    <h1>Smart Car Control</h1>\n"
    "    <div class=\"card\">\n"
    "      <div class=\"row\">\n"
    "        <button data-mode=\"stop\">Stop</button>\n"
    "        <button data-mode=\"trace\">Trace</button>\n"
    "        <button data-mode=\"obstacle\">Obstacle</button>\n"
    "        <button data-mode=\"remote\">Remote</button>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <div class=\"kv\"><span>Motor</span><span id=\"motorVal\">0</span></div>\n"
    "        <input id=\"motor\" type=\"range\" min=\"-100\" max=\"100\" value=\"0\"/>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <div class=\"kv\"><span>Steer</span><span id=\"steerVal\">0</span></div>\n"
    "        <input id=\"steer\" type=\"range\" min=\"-100\" max=\"100\" value=\"0\"/>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <div class=\"hint\">Keys: W/S motor, A/D steer, Space reset</div>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <div class=\"hint\">This page sends /api/ctrl every 25ms when Remote is active.</div>\n"
    "      </div>\n"
    "    </div>\n"
    "  </div>\n"
    "  <script src=\"/app.js\"></script>\n"
    "</body>\n"
    "</html>\n";

static const char g_style_css[] =
    "html,body{margin:0;padding:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;}\n"
    ".wrap{max-width:720px;margin:0 auto;padding:24px;}\n"
    "h1{margin:0 0 16px 0;font-size:24px;}\n"
    ".card{border:1px solid #e5e7eb;border-radius:12px;padding:16px;box-shadow:0 2px 10px rgba(0,0,0,.04);}\n"
    ".row{display:flex;gap:12px;align-items:center;margin:10px 0;}\n"
    "button{padding:10px 12px;border:1px solid #d1d5db;border-radius:10px;background:#fff;cursor:pointer;}\n"
    "button.active{background:#111827;color:#fff;border-color:#111827;}\n"
    "input[type=range]{flex:1;}\n"
    ".kv{width:120px;display:flex;justify-content:space-between;color:#111827;font-weight:600;}\n"
    ".hint{color:#6b7280;font-size:12px;}\n";

static const char g_app_js[] =
    "let activeMode='stop';\n"
    "const motor=document.getElementById('motor');\n"
    "const steer=document.getElementById('steer');\n"
    "const motorVal=document.getElementById('motorVal');\n"
    "const steerVal=document.getElementById('steerVal');\n"
    "const btns=[...document.querySelectorAll('button[data-mode]')];\n"
    "function setMode(m){activeMode=m;btns.forEach(b=>b.classList.toggle('active',b.dataset.mode===m));\n"
    "fetch(`/api/mode?val=${encodeURIComponent(m)}`).catch(()=>{});}\n"
    "btns.forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.mode)));\n"
    "function clamp(v){v=Number(v);if(v>100)v=100;if(v<-100)v=-100;return v|0;}\n"
    "function updateLabels(){motorVal.textContent=motor.value;steerVal.textContent=steer.value;}\n"
    "motor.addEventListener('input',updateLabels);steer.addEventListener('input',updateLabels);updateLabels();\n"
    "window.addEventListener('keydown',e=>{\n"
    "  if(e.key==='w'||e.key==='W'){motor.value=clamp(Number(motor.value)+10);}\n"
    "  else if(e.key==='s'||e.key==='S'){motor.value=clamp(Number(motor.value)-10);}\n"
    "  else if(e.key==='a'||e.key==='A'){steer.value=clamp(Number(steer.value)+10);}\n"
    "  else if(e.key==='d'||e.key==='D'){steer.value=clamp(Number(steer.value)-10);}\n"
    "  else if(e.key===' '){motor.value=0;steer.value=0;}\n"
    "  updateLabels();\n"
    "});\n"
    "setInterval(()=>{\n"
    "  if(activeMode!=='remote') return;\n"
    "  const m=clamp(motor.value);\n"
    "  const s1=clamp(steer.value);\n"
    "  const s2=0;\n"
    "  fetch(`/api/ctrl?m=${m}&s1=${s1}&s2=${s2}`).catch(()=>{});\n"
    "},25);\n"
    "setMode('stop');\n";

static void http_send_response(int fd, const char *status, const char *ctype, const char *body)
{
    char header[256] = {0};
    size_t body_len = (body == NULL) ? 0 : strlen(body);

    (void)snprintf(header, sizeof(header),
                   "HTTP/1.1 %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                   "Access-Control-Allow-Headers: *\r\n"
                   "Access-Control-Max-Age: 86400\r\n"
                   "\r\n",
                   status, ctype, (unsigned)body_len);

    (void)lwip_send(fd, header, strlen(header), 0);
    if (body_len > 0) {
        (void)lwip_send(fd, body, body_len, 0);
    }
}

static void http_send_no_content(int fd)
{
    http_send_response(fd, "204 No Content", "text/plain", "");
}

static const char *query_get(const char *query, const char *key)
{
    if (query == NULL || key == NULL) {
        return NULL;
    }

    size_t klen = strlen(key);
    const char *p = query;
    while (*p != '\0') {
        if ((strncmp(p, key, klen) == 0) && p[klen] == '=') {
            return p + klen + 1;
        }
        const char *amp = strchr(p, '&');
        if (amp == NULL) {
            break;
        }
        p = amp + 1;
    }

    return NULL;
}

static int parse_i8(const char *s, int8_t *out)
{
    if (s == NULL || out == NULL) {
        return -1;
    }

    int v = atoi(s);
    if (v > 100) {
        v = 100;
    } else if (v < -100) {
        v = -100;
    }

    *out = (int8_t)v;
    return 0;
}

static void handle_api_mode(int fd, const char *query)
{
    const char *val = query_get(query, "val");
    if (val == NULL) {
        http_send_response(fd, "400 Bad Request", "text/plain", "missing val\n");
        return;
    }

    if (strncmp(val, "stop", 4) == 0) {
        robot_mgr_set_status(CAR_STOP_STATUS);
    } else if (strncmp(val, "trace", 5) == 0) {
        robot_mgr_set_status(CAR_TRACE_STATUS);
    } else if (strncmp(val, "obstacle", 8) == 0) {
        robot_mgr_set_status(CAR_OBSTACLE_AVOIDANCE_STATUS);
    } else if (strncmp(val, "remote", 6) == 0) {
        robot_mgr_set_status(CAR_WIFI_CONTROL_STATUS);
    } else {
        http_send_response(fd, "400 Bad Request", "text/plain", "invalid mode\n");
        return;
    }

    http_send_response(fd, "200 OK", "application/json", "{\"ok\":true}\n");
}

static void handle_api_ctrl(int fd, const char *query)
{
    int8_t m = 0;
    int8_t s1 = 0;
    int8_t s2 = 0;

    (void)parse_i8(query_get(query, "m"), &m);
    (void)parse_i8(query_get(query, "s1"), &s1);
    (void)parse_i8(query_get(query, "s2"), &s2);

    net_service_push_cmd(m, s1, s2);
    robot_mgr_set_status(CAR_WIFI_CONTROL_STATUS);

    http_send_response(fd, "200 OK", "application/json", "{\"ok\":true}\n");
}

static void handle_http(int fd, const char *method, const char *path, const char *query)
{
    if (strcmp(method, "OPTIONS") == 0) {
        http_send_no_content(fd);
        return;
    }

    if (strcmp(path, "/") == 0) {
        http_send_response(fd, "200 OK", "text/html", g_index_html);
        return;
    }
    if (strcmp(path, "/style.css") == 0) {
        http_send_response(fd, "200 OK", "text/css", g_style_css);
        return;
    }
    if (strcmp(path, "/app.js") == 0) {
        http_send_response(fd, "200 OK", "application/javascript", g_app_js);
        return;
    }
    if (strcmp(path, "/api/mode") == 0) {
        handle_api_mode(fd, query);
        return;
    }
    if (strcmp(path, "/api/ctrl") == 0) {
        handle_api_ctrl(fd, query);
        return;
    }

    http_send_response(fd, "404 Not Found", "text/plain", "not found\n");
}

static void *http_ctrl_task(const char *arg)
{
    UNUSED(arg);

    int listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        printf("http_ctrl: socket create failed, err=%d\r\n", errno);
        return NULL;
    }

    int opt = 1;
    (void)lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(HTTP_CTRL_LISTEN_PORT);
    addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

    if (lwip_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("http_ctrl: bind failed, err=%d\r\n", errno);
        lwip_close(listen_fd);
        return NULL;
    }

    if (lwip_listen(listen_fd, 2) < 0) {
        printf("http_ctrl: listen failed, err=%d\r\n", errno);
        lwip_close(listen_fd);
        return NULL;
    }

    printf("http_ctrl: listening on %d\r\n", HTTP_CTRL_LISTEN_PORT);

    while (1) {
        int client_fd = lwip_accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            osal_msleep(20);
            continue;
        }

        char req[512] = {0};
        ssize_t n = lwip_recv(client_fd, req, sizeof(req) - 1, 0);
        if (n <= 0) {
            lwip_close(client_fd);
            continue;
        }

        req[n] = '\0';

        char method[8] = {0};
        char uri[256] = {0};
        if (sscanf(req, "%7s %255s", method, uri) != 2) {
            http_send_response(client_fd, "400 Bad Request", "text/plain", "bad request\n");
            lwip_close(client_fd);
            continue;
        }

        if ((strcmp(method, "GET") != 0) && (strcmp(method, "OPTIONS") != 0)) {
            http_send_response(client_fd, "405 Method Not Allowed", "text/plain", "only GET/OPTIONS\n");
            lwip_close(client_fd);
            continue;
        }

        char path[256] = {0};
        const char *query = NULL;
        const char *qm = strchr(uri, '?');
        if (qm != NULL) {
            size_t plen = (size_t)(qm - uri);
            if (plen >= sizeof(path)) {
                plen = sizeof(path) - 1;
            }
            (void)memcpy_s(path, sizeof(path), uri, plen);
            path[plen] = '\0';
            query = qm + 1;
        } else {
            (void)strncpy_s(path, sizeof(path), uri, sizeof(path) - 1);
            query = NULL;
        }

        handle_http(client_fd, method, path, query);
        lwip_close(client_fd);
    }

    return NULL;
}

void http_ctrl_service_init(void)
{
    if (g_http_started) {
        return;
    }

    osal_kthread_lock();
    g_http_task_handle = osal_kthread_create((osal_kthread_handler)http_ctrl_task, NULL, "http_ctrl_task",
                                            HTTP_CTRL_STACK_SIZE);
    if (g_http_task_handle != NULL) {
        (void)osal_kthread_set_priority(g_http_task_handle, HTTP_CTRL_TASK_PRIORITY);
        g_http_started = true;
    } else {
        printf("http_ctrl: failed to create task\r\n");
    }
    osal_kthread_unlock();
}
