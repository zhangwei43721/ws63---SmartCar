/**
 * @file        mode_obstacle.c
 * @brief       避障模式实现
 * @note        使用通用模式框架，简化代码结构
 */

#include "mode_obstacle.h"
#include "mode_common.h"
#include "robot_config.h"
#include "robot_mgr.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"

#include <stdio.h>

// 上次遥测时间
static unsigned long long g_last_telemetry_time = 0;

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

    return (left_dist > right_dist) ? CAR_TURN_LEFT : CAR_TURN_RIGHT;
}

/**
 * @brief 根据距离执行避障动作
 */
static void perform_obstacle_avoidance(float distance)
{
    // 避障阈值可由 NV 配置动态调整
    float threshold = (float)robot_mgr_get_obstacle_threshold_cm();

    // 如果获取的阈值为0（异常），使用默认避障阈值
    if (threshold <= 0) {
        threshold = DISTANCE_BETWEEN_CAR_AND_OBSTACLE;
    }

    if (distance > 0 && distance < threshold) {
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
    } else
        car_forward(); // 路径通畅，继续直行
}

/**
 * @brief 避障模式进入函数
 */
void mode_obstacle_enter(void)
{
    printf("进入避障模式...\r\n");
    g_last_telemetry_time = 0;
    set_servo_angle_wait(SERVO_MIDDLE_ANGLE);
}

/**
 * @brief 避障模式周期回调函数
 */
void mode_obstacle_tick(void)
{
    float distance = get_distance_update();
    perform_obstacle_avoidance(distance);

    // 定期上报遥测数据
    unsigned long long now = osal_get_jiffies();
    if (now - g_last_telemetry_time > osal_msecs_to_jiffies(TELEMETRY_REPORT_MS)) {
        int dist_x100 = (int)(distance * 100.0f);
        // printf("[obstacle] dist=%d.%02dcm\r\n", dist_x100 / 100, dist_x100 % 100);

        char data[64];
        snprintf(data, sizeof(data), "\"dist_x100\":%d", dist_x100);
        send_telemetry("obstacle", data);
        g_last_telemetry_time = now;
    }
}

/**
 * @brief 避障模式退出函数
 */
void mode_obstacle_exit(void)
{
    car_stop();
    // 退出时舵机回中
    set_servo_angle_wait(SERVO_MIDDLE_ANGLE);
}
