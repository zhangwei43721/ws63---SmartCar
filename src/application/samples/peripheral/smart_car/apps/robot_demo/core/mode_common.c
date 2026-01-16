/**
 ****************************************************************************************************
 * @file        mode_common.c
 * @brief       模式运行通用框架实现
 ****************************************************************************************************
 */

#include "mode_common.h"
#include "../core/robot_mgr.h"
#include "../services/net_service.h"
#include "robot_config.h"
#include "securec.h"

void mode_run_loop(int expected_status, ModeRunFunc run_func, ModeExitFunc exit_func,
                   unsigned int telemetry_interval_ms)
{
    if (run_func == NULL) {
        return;
    }

    ModeContext ctx = {0};
    ctx.enter_time = osal_get_jiffies();
    ctx.last_telemetry_time = ctx.enter_time;
    ctx.is_running = true;

    while (robot_mgr_get_status() == expected_status && ctx.is_running) {
        run_func(&ctx);

        unsigned long long now = osal_get_jiffies();
        if (now - ctx.last_telemetry_time > osal_msecs_to_jiffies(telemetry_interval_ms)) {
            ctx.last_telemetry_time = now;
        }

        osal_msleep(MAIN_LOOP_DELAY_MS);
    }

    if (exit_func != NULL) {
        exit_func(&ctx);
    }
}

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
