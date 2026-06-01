/**
 * @file        captive_portal_control.c
 * @brief       AP 模式下的小车 HTTP 控制接口实现
 * @details     内嵌控制页面 + /api/status /api/mode /api/move REST 接口
 */

#include "captive_portal_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "lwip/sockets.h"
#include "udp_net_common.h"

// ---------- 内嵌控制页面 ----------
static const char s_html_control[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>小车控制</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe "
    "UI,Caro,sans-serif;background:#f2f3f5;min-height:100vh;padding:16px}"
    ".card{background:#fff;border-radius:16px;box-shadow:0 4px 20px "
    "rgba(0,0,0,.08);padding:20px;max-width:360px;margin:0 auto}"
    "h1{font-size:20px;color:#1a1a1a;margin-bottom:16px;text-align:center}"
    ".mode-box{display:flex;gap:8px;margin-bottom:16px}"
    ".mode-btn{flex:1;padding:10px "
    "4px;border:none;border-radius:8px;background:#e5e5ea;color:#333;font-size:13px;cursor:pointer}"
    ".mode-btn.on{background:#007aff;color:#fff}"
    ".sensor-box{background:#f8f8f8;border-radius:12px;padding:16px;margin-bottom:16px;min-height:80px;text-align:"
    "center}"
    ".sensor-box h3{font-size:14px;color:#666;margin-bottom:10px}"
    ".trace-leds{display:flex;justify-content:center;gap:24px}"
    ".trace-led{width:36px;height:36px;border-radius:50%;background:#ddd;border:3px solid #ccc}"
    ".trace-led.on{background:#111;border-color:#34c759;box-shadow:0 0 8px #34c759}"
    ".avoid-num{font-size:36px;font-weight:bold;color:#007aff}"
    ".avoid-bar{width:100%;height:8px;background:#ddd;border-radius:4px;margin-top:8px;overflow:hidden}"
    ".avoid-bar-in{height:100%;background:#34c759;width:0%;border-radius:4px;transition:width .3s}"
    ".dpad{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;max-width:280px;margin:0 auto}"
    ".dpad-btn{height:64px;border:none;border-radius:12px;background:#fff;box-shadow:0 4px 12px "
    "rgba(0,0,0,.1);font-size:22px;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:none}"
    ".dpad-btn:active{background:#007aff;color:#fff}"
    ".dpad-btn.stop{background:#ff3b30;color:#fff}"
    ".tip{margin-top:12px;font-size:12px;color:#999;text-align:center}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>小车控制</h1>"
    "<div style=\"text-align:center;margin-bottom:12px\">"
    "<a href=\"#\" onclick=\"resetMode();return false;\" "
    "style=\"font-size:13px;color:#007aff;text-decoration:none\">返回配网</a>"
    "</div>"
    "<div class=\"mode-box\">"
    "<button class=\"mode-btn\" id=\"m0\" onclick=\"setMode(0)\">停止</button>"
    "<button class=\"mode-btn\" id=\"m3\" onclick=\"setMode(3)\">遥控</button>"
    "<button class=\"mode-btn\" id=\"m1\" onclick=\"setMode(1)\">循迹</button>"
    "<button class=\"mode-btn\" id=\"m2\" onclick=\"setMode(2)\">避障</button>"
    "</div>"
    "<div class=\"sensor-box\" id=\"sensorBox\">"
    "<div id=\"sDefault\" style=\"display:none\">"
    "<h3>当前模式</h3>"
    "<p style=\"color:#999;font-size:14px\" id=\"modeText\">待机</p>"
    "</div>"
    "<div id=\"sTrace\" style=\"display:none\">"
    "<h3>循迹传感器</h3>"
    "<div class=\"trace-leds\">"
    "<div class=\"trace-led\" id=\"tL\"></div>"
    "<div class=\"trace-led\" id=\"tM\"></div>"
    "<div class=\"trace-led\" id=\"tR\"></div>"
    "</div>"
    "<p style=\"color:#999;font-size:11px;margin-top:8px\">黑色 = 检测到线</p>"
    "</div>"
    "<div id=\"sAvoid\" style=\"display:none\">"
    "<h3>前方障碍物</h3>"
    "<div class=\"avoid-num\"><span id=\"aDist\">--</span><span style=\"font-size:16px;color:#999\"> cm</span></div>"
    "<div class=\"avoid-bar\"><div class=\"avoid-bar-in\" id=\"aBar\"></div></div>"
    "</div>"
    "</div>"
    "<div class=\"dpad\" id=\"dpad\">"
    "<button class=\"dpad-btn\" style=\"grid-column:2\" onpointerdown=\"move(100,100)\" "
    "onpointerup=\"move(0,0)\">上</button>"
    "<button class=\"dpad-btn\" style=\"grid-column:1;grid-row:2\" onpointerdown=\"move(0,100)\" "
    "onpointerup=\"move(0,0)\">左</button>"
    "<button class=\"dpad-btn stop\" style=\"grid-column:2;grid-row:2\" onclick=\"move(0,0)\">停</button>"
    "<button class=\"dpad-btn\" style=\"grid-column:3;grid-row:2\" onpointerdown=\"move(100,0)\" "
    "onpointerup=\"move(0,0)\">右</button>"
    "<button class=\"dpad-btn\" style=\"grid-column:2;grid-row:3\" onpointerdown=\"move(-100,-100)\" "
    "onpointerup=\"move(0,0)\">下</button>"
    "</div>"
    "<p class=\"tip\">按住方向键控制，松开自动停止</p>"
    "</div>"
    "<script>"
    "var curMode=-1;var pendingMove=null;"
    "function setMode(m){fetch('/api/mode?m='+m).then(function(){poll();});}"
    "function resetMode(){fetch('/api/reset').then(function(){location.href='/';});}"
    "function move(l,r){if(pendingMove){pendingMove.abort();}pendingMove=new "
    "AbortController();fetch('/api/move?l='+l+'&r='+r,{signal:pendingMove.signal}).catch(function(){});}"
    "function poll(){"
    "fetch('/api/status').then(function(r){return r.json();}).then(function(d){"
    "curMode=d.mode;var names=['停止','循迹','避障','遥控'];"
    "document.getElementById('modeText').textContent=names[d.mode]||'未知';"
    "['m0','m1','m2','m3'].forEach(function(id,i){document.getElementById(id).className='mode-btn'+(i==d.mode?' "
    "on':'');});"
    "document.getElementById('sDefault').style.display=(d.mode==0||d.mode==3)?'block':'none';"
    "document.getElementById('sTrace').style.display=d.mode==1?'block':'none';"
    "document.getElementById('sAvoid').style.display=d.mode==2?'block':'none';"
    "if(d.mode==1){"
    "document.getElementById('tL').className='trace-led'+(d.ir[0]?'':' on');"
    "document.getElementById('tM').className='trace-led'+(d.ir[1]?'':' on');"
    "document.getElementById('tR').className='trace-led'+(d.ir[2]?'':' on');}"
    "if(d.mode==2){"
    "document.getElementById('aDist').textContent=d.dist.toFixed(1);"
    "var pct=Math.min(d.dist/50*100,100);"
    "document.getElementById('aBar').style.width=pct+'%';"
    "document.getElementById('aBar').style.background=d.dist<20?'#ff3b30':(d.dist<40?'#ff9500':'#34c759');}"
    "}).catch(function(){});}"
    "setInterval(poll,500);"
    "poll();"
    "</script>"
    "</body></html>";

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
    car_mgr_get_state_copy(&st);

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
        car_mgr_post_mode((CarStatus)mode, MODE_SRC_HTTP);
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

    // 推入 Motor Executor 命令队列，由独立高优先级任务执行
    bsp_motor_push_cmd((int8_t)left, (int8_t)right);

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

/* 处理控制页面及 REST API 请求，匹配路径则处理并返回 true */
bool captive_portal_control_handle(int client_fd, bool is_get, const char *path, const char *query)
{
    if (is_get && strcmp(path, "/control") == 0) {
        http_send_response_and_close(client_fd, s_html_control);
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
