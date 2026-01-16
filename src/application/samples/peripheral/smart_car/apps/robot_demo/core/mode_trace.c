/**
 ****************************************************************************************************
 * @file        mode_trace.c
 * @brief       循迹模式实现 - 重构版
 * @note        使用通用模式框架，简化代码结构
 ****************************************************************************************************
 */

#include "mode_trace.h"
#include "mode_common.h"
#include "robot_config.h"

/**
 * @brief 循迹模式运行函数（通用框架回调）
 */
static void trace_run_func(ModeContext *ctx)
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
    if (now - ctx->last_telemetry_time > osal_msecs_to_jiffies(TELEMETRY_REPORT_MS)) {
        printf("[trace] L=%u M=%u R=%u\r\n", left, middle, right);

        char data[64];
        snprintf(data, sizeof(data), "\"left\":%u,\"mid\":%u,\"right\":%u", left, middle, right);
        send_telemetry("trace", data);
    }
}

/**
 * @brief 循迹模式退出函数（通用框架回调）
 */
static void trace_exit_func(ModeContext *ctx)
{
    UNUSED(ctx);
    car_stop();
}

/**
 * @brief 循迹模式主运行函数
 */
void mode_trace_run(void)
{
    printf("进入循迹模式...\r\n");
    mode_run_loop(CAR_TRACE_STATUS, trace_run_func, trace_exit_func, TELEMETRY_REPORT_MS);
}

/**
 * @brief 循迹模式周期回调函数（空实现）
 */
void mode_trace_tick(void)
{
    // 此模式下主要逻辑在 run 线程中，tick 可留空
}
