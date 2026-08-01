#include "debug_log_service.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "soc_osal.h"
#include "../channels/udp_channel.h"
#include "securec.h"

static osal_mutex s_log_mutex;
static bool s_log_inited = false;

// 初始化调试日志服务
void debug_log_init(void)
{
    if (s_log_inited) {
        return;
    }
    if (osal_mutex_init(&s_log_mutex) == OSAL_SUCCESS) {
        s_log_inited = true;
    }
}

// 线程安全的日志写入接口（仅通过 UDP 发送日志包，避免阻塞物理串口）
void car_log(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }

    // 打包成 0x0B 协议帧发送到已配对的主机 Proxy 
    if (s_log_inited) {
        (void)osal_mutex_lock(&s_log_mutex);

        // 帧格式: [0x0B, text...]
        uint8_t packet[260];
        packet[0] = 0x0B; // CAR_PKT_LOG_DATA
        size_t cpy_len = ((size_t)len < sizeof(buf)) ? (size_t)len : (sizeof(buf) - 1);
        (void)memcpy_s(&packet[1], sizeof(packet) - 1, buf, cpy_len);

        udp_channel_send_data(packet, cpy_len + 1);

        (void)osal_mutex_unlock(&s_log_mutex);
    }
}
