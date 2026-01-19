#include "mode_trace.h"
#include "mode_common.h"
#include "robot_config.h"
#include "robot_mgr.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include <stdio.h>

static unsigned long long g_last_telemetry_time = 0;

void mode_trace_enter(void)
{
    printf("进入循迹模式...\r\n");
    g_last_telemetry_time = 0;
}

/*  
 * 循迹模式主循环
*/
void mode_trace_tick(void)
{
    unsigned int left = tcrt5000_get_left();
    unsigned int middle = tcrt5000_get_middle();
    unsigned int right = tcrt5000_get_right();

    // 更新红外传感器状态到全局状态
    robot_mgr_update_ir_status(left, middle, right);

    // 根据传感器状态控制小车
    if (middle == TCRT5000_ON_BLACK)
        car_forward();
    else if (left == TCRT5000_ON_BLACK)
        car_left();
    else if (right == TCRT5000_ON_BLACK)
        car_right();
    else
        car_stop();

    // 定期上报遥测数据
    unsigned long long now = osal_get_jiffies();
    if (now - g_last_telemetry_time > osal_msecs_to_jiffies(TELEMETRY_REPORT_MS)) {
        // printf("[trace] L=%u M=%u R=%u\r\n", left, middle, right);

        char data[64];
        snprintf(data, sizeof(data), "\"left\":%u,\"mid\":%u,\"right\":%u", left, middle, right);
        send_telemetry("trace", data);
        g_last_telemetry_time = now;
    }
}

void mode_trace_exit(void)
{
    car_stop();
}
