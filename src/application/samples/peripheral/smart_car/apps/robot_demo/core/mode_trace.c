#include "mode_trace.h"
#include "mode_common.h"
#include "robot_config.h"
#include "robot_mgr.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include <stdio.h>

static unsigned long long g_last_telemetry_time = 0;
static unsigned long long g_last_seen_tick = 0;

void mode_trace_enter(void)
{
    printf("进入循迹模式...\r\n");
    g_last_telemetry_time = 0;
    g_last_seen_tick = osal_get_jiffies();
}

/*
 * 循迹模式主循环
 */
// 根据用户指示：黑色是高电平
#define TRACE_DETECT_BLACK 1

// 循迹速度参数配置
#define TRACE_SPEED_FORWARD     40  // 直行速度
#define TRACE_SPEED_TURN_FAST   50  // 转向外侧轮速度
#define TRACE_SPEED_TURN_SLOW   10  // 转向内侧轮速度
#define TRACE_LOST_TIMEOUT_MS   300 // 丢失黑线后继续行驶的超时时间(ms)

void mode_trace_tick(void)
{
    unsigned int left = tcrt5000_get_left();
    unsigned int middle = tcrt5000_get_middle();
    unsigned int right = tcrt5000_get_right();
    unsigned long long now = osal_get_jiffies();

    // 更新红外传感器状态到全局状态
    robot_mgr_update_ir_status(left, middle, right);

    // 根据传感器状态控制小车 (优先级：中间 > 左 > 右)
    // 逻辑：如果中间检测到黑线，说明位置正确，直行
    // 如果左边检测到黑线，说明车身偏右，需要左转纠正
    // 如果右边检测到黑线，说明车身偏左，需要右转纠正
    if (middle == TRACE_DETECT_BLACK) {
        // 直行：双轮同速
        l9110s_set_differential(TRACE_SPEED_FORWARD, TRACE_SPEED_FORWARD);
        g_last_seen_tick = now;
    } else if (left == TRACE_DETECT_BLACK) {
        // 左转：左轮慢，右轮快 (差速)
        l9110s_set_differential(TRACE_SPEED_TURN_SLOW, TRACE_SPEED_TURN_FAST);
        g_last_seen_tick = now;
    } else if (right == TRACE_DETECT_BLACK) {
        // 右转：左轮快，右轮慢 (差速)
        l9110s_set_differential(TRACE_SPEED_TURN_FAST, TRACE_SPEED_TURN_SLOW);
        g_last_seen_tick = now;
    } else {
        // 未检测到黑线
        // 记忆功能：如果刚丢失信号不久，继续直行一段距离，尝试找回黑线
        if (now - g_last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
            l9110s_set_differential(TRACE_SPEED_FORWARD, TRACE_SPEED_FORWARD);
        } else {
            // 超时仍未找到，停车
            l9110s_set_differential(0, 0);
        }
    }

    // 定期上报遥测数据
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
