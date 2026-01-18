/**
 ****************************************************************************************************
 * @file        mode_obstacle.c
 * @brief       避障模式实现 - 重构版
 * @note        使用通用模式框架，简化代码结构
 ****************************************************************************************************
 */

#include "mode_obstacle.h"
#include "mode_common.h"
#include "robot_config.h"
#include "robot_mgr.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"

#include <stdio.h>

/**
 * @brief 设置舵机角度并等待到位，同时更新全局状态
 */
static void set_servo_angle_wait(int angle)
{
    sg90_set_angle(angle);
    robot_mgr_update_servo_angle(angle);
    osal_msleep(SERVO_MOVE_DELAY_MS);
}

/**
 * @brief 获取并更新距离信息
 */
static float get_distance_update(void)
{
    float dist = hcsr04_get_distance();
    robot_mgr_update_distance(dist);
    return dist;
}

/**
 * @brief 停车扫描判断方向
 * @return CAR_TURN_LEFT (左侧宽敞) 或 CAR_TURN_RIGHT (右侧宽敞)
 */
static unsigned int scan_and_decide_direction(void)
{
    // 扫描左侧
    set_servo_angle_wait(SERVO_LEFT_ANGLE);
    osal_msleep(SENSOR_STABILIZE_MS);
    float left_dist = get_distance_update();

    // 扫描右侧
    set_servo_angle_wait(SERVO_RIGHT_ANGLE);
    osal_msleep(SENSOR_STABILIZE_MS);
    float right_dist = get_distance_update();

    // 舵机回中
    set_servo_angle_wait(SERVO_MIDDLE_ANGLE);

    // // 打印调试信息
    // int l_val = (int)(left_dist * 100);
    // int r_val = (int)(right_dist * 100);
    // printf("扫描结果: 左=%d.%02d cm, 右=%d.%02d cm\r\n", l_val / 100, l_val % 100, r_val / 100, r_val % 100);

    return (left_dist > right_dist) ? CAR_TURN_LEFT : CAR_TURN_RIGHT;
}

/**
 * @brief 根据距离执行避障动作
 */
static void perform_obstacle_avoidance(float distance)
{
    if (distance <= 0 || distance < DISTANCE_BETWEEN_CAR_AND_OBSTACLE) {
        // 停车并后退
        car_stop();
        osal_msleep(200);
        car_backward();
        osal_msleep(BACKWARD_MOVE_MS);
        car_stop();

        // 扫描环境决定转向
        unsigned int direction = scan_and_decide_direction();

        // 执行转向
        if (direction == CAR_TURN_LEFT)
            car_left();
        else
            car_right();

        osal_msleep(TURN_MOVE_MS);
        car_stop();
    } else {
        // 路径通畅，继续直行
        car_forward();
    }
}

/**
 * @brief 避障模式运行函数（通用框架回调）
 */
static void obstacle_run_func(ModeContext *ctx)
{
    float distance = get_distance_update();
    perform_obstacle_avoidance(distance);

    // 定期上报遥测数据
    unsigned long long now = osal_get_jiffies();
    if (now - ctx->last_telemetry_time > osal_msecs_to_jiffies(TELEMETRY_REPORT_MS)) {
        int dist_x100 = (int)(distance * 100.0f);
        printf("[obstacle] dist=%d.%02dcm\r\n", dist_x100 / 100, dist_x100 % 100);

        char data[64];
        snprintf(data, sizeof(data), "\"dist_x100\":%d", dist_x100);
        send_telemetry("obstacle", data);
    }
}

/**
 * @brief 避障模式退出函数（通用框架回调）
 */
static void obstacle_exit_func(ModeContext *ctx)
{
    UNUSED(ctx);
    car_stop();
    set_servo_angle_wait(SERVO_MIDDLE_ANGLE);
}

/**
 * @brief 避障模式主运行函数
 */
void mode_obstacle_run(void)
{
    printf("进入避障模式...\r\n");
    set_servo_angle_wait(SERVO_MIDDLE_ANGLE);
    mode_run_loop(CAR_OBSTACLE_AVOIDANCE_STATUS, obstacle_run_func, obstacle_exit_func, TELEMETRY_REPORT_MS);
}

/**
 * @brief 避障模式周期回调函数（空实现）
 */
void mode_obstacle_tick(void)
{
    // 此模式下主要逻辑在 run 线程中，tick 可留空
}
