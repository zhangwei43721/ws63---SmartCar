#include "mode_common.h"
#include "../services/net_service.h"
#include <stdio.h>

/**
 * @brief 发送遥测数据
 * @param mode 模式名称
 * @param data JSON格式的额外数据
 */
void send_telemetry(const char *mode, const char *data)
{
    if (mode == NULL || data == NULL) {
        return;
    }

    char payload[PAYLOAD_BUFFER_SIZE];
    int len = snprintf(payload, sizeof(payload), "{\"mode\":\"%s\",%s}\n", mode, data);
    if (len > 0 && len < (int)sizeof(payload)) {
        net_service_send_text(payload);
    }
}
