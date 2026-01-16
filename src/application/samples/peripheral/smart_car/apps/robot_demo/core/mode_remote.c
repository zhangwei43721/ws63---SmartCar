/**
 ****************************************************************************************************
 * @file        mode_remote.c
 * @brief       遥控模式实现 - 重构版
 * @note        使用通用模式框架，简化代码结构
 ****************************************************************************************************
 */

#include "mode_remote.h"
#include "mode_common.h"
#include "robot_config.h"
#include "../services/udp_service.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"

// 遥控命令超时时间
static unsigned long long g_last_cmd_tick = 0;

/**
 * @brief 应用差速控制
 * @param motor1 左电机速度 (-100~100)
 * @param motor2 右电机速度 (-100~100)
 * @param servo 舵机角度 (0~180)
 */
static void apply_differential_control(int8_t motor1, int8_t motor2, int8_t servo)
{
    // 应用差速控制
    l9110s_set_differential(motor1, motor2);

    // 应用舵机控制
    sg90_set_angle((unsigned int)servo);
    robot_mgr_update_servo_angle((unsigned int)servo);
}

/**
 * @brief 遥控模式运行函数（通用框架回调）
 */
static void remote_run_func(ModeContext *ctx)
{
    UNUSED(ctx);

    int8_t motor1, motor2, servo;
    if (udp_service_pop_cmd(&motor1, &motor2, &servo)) {
        apply_differential_control(motor1, motor2, servo);
        g_last_cmd_tick = osal_get_jiffies();
    }

    // 命令超时自动停车
    unsigned long long now = osal_get_jiffies();
    if (now - g_last_cmd_tick > osal_msecs_to_jiffies(REMOTE_CMD_TIMEOUT_MS)) {
        l9110s_set_differential(0, 0);  // 双电机停止
        sg90_set_angle(SERVO_MIDDLE_ANGLE);
        robot_mgr_update_servo_angle(SERVO_MIDDLE_ANGLE);
    }
}

/**
 * @brief 遥控模式退出函数（通用框架回调）
 */
static void remote_exit_func(ModeContext *ctx)
{
    UNUSED(ctx);
    car_stop();
}

/**
 * @brief 遥控模式主运行函数
 */
void mode_remote_run(void)
{
    printf("进入 WiFi 遥控模式...\r\n");
    car_stop();
    g_last_cmd_tick = osal_get_jiffies();
    mode_run_loop(CAR_WIFI_CONTROL_STATUS, remote_run_func, remote_exit_func, TELEMETRY_REPORT_MS);
}

/**
 * @brief 遥控模式周期回调函数（空实现）
 */
void mode_remote_tick(void)
{
    // 此模式下主要逻辑在 run 线程中，tick 可留空
}
