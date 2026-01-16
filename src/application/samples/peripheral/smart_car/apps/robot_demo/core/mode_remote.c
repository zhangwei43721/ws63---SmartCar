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

// 遥控命令超时时间
static unsigned long long g_last_cmd_tick = 0;

/**
 * @brief 执行电机控制命令
 * @param cmd 电机命令值（正数前进，负数后退，0停止）
 */
static void apply_motor_cmd(int8_t cmd)
{
    if (cmd > 0)
        car_forward();
    else if (cmd < 0)
        car_backward();
    else
        car_stop();
}

/**
 * @brief 执行舵机控制命令
 * @param val 舵机命令值（-100到100，0为中心）
 */
static void apply_servo_cmd(int8_t val)
{
    if (val == 0) {
        sg90_set_angle(SERVO_MIDDLE_ANGLE);
        robot_mgr_update_servo_angle(SERVO_MIDDLE_ANGLE);
        return;
    }

    int sign = (val > 0) ? 1 : -1;
    int mag = (val > 0) ? val : -val;
    if (mag > 100)
        mag = 100;

    int offset = (mag * 90) / 100;
    int angle = SERVO_MIDDLE_ANGLE + (sign * offset);

    if (angle < (int)SG90_ANGLE_MIN)
        angle = SG90_ANGLE_MIN;
    else if (angle > (int)SG90_ANGLE_MAX)
        angle = SG90_ANGLE_MAX;

    sg90_set_angle((unsigned int)angle);
    robot_mgr_update_servo_angle((unsigned int)angle);
}

/**
 * @brief 遥控模式运行函数（通用框架回调）
 */
static void remote_run_func(ModeContext *ctx)
{
    UNUSED(ctx);

    int8_t motor, servo1, servo2;
    if (net_service_pop_cmd(&motor, &servo1, &servo2)) {
        apply_motor_cmd(motor);
        apply_servo_cmd(servo1);
        (void)servo2;
        g_last_cmd_tick = osal_get_jiffies();
    }

    // 命令超时自动停车
    unsigned long long now = osal_get_jiffies();
    if (now - g_last_cmd_tick > osal_msecs_to_jiffies(REMOTE_CMD_TIMEOUT_MS))
        car_stop();
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
