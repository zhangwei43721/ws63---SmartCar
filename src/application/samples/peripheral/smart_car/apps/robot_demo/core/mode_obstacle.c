/**
 * @file        mode_obstacle.c
 * @brief       避障模式实现
 */

#include "mode_obstacle.h"
#include "robot_config.h"
#include "robot_mgr.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"

#include <stdio.h>

/**
 * @brief 避障模式进入函数
 */
void mode_obstacle_enter(void)
{
    printf("进入避障模式...\r\n");
    // 舵机回中
    sg90_set_angle(SERVO_CENTER);
    robot_mgr_update_servo_angle(SERVO_CENTER);
    osal_msleep(SERVO_DELAY);
}

/**
 * @brief 避障模式周期回调函数
 */
void mode_obstacle_tick(void)
{
    // 获取距离并更新状态
    float distance = hcsr04_get_distance();
    robot_mgr_update_distance(distance);

    // 避障阈值
    float threshold = (float)OBSTACLE_DISTANCE;

    if (distance > 0 && distance < threshold) {
        // 有障碍物：停车并后退
        CAR_STOP();
        osal_msleep(200);
        CAR_BACKWARD();
        osal_msleep(BACKWARD_TIME);
        CAR_STOP();

        // 扫描左侧
        sg90_set_angle(SERVO_LEFT);
        robot_mgr_update_servo_angle(SERVO_LEFT);
        osal_msleep(SERVO_DELAY);
        float left_dist = hcsr04_get_distance();

        // 扫描右侧
        sg90_set_angle(SERVO_RIGHT);
        robot_mgr_update_servo_angle(SERVO_RIGHT);
        osal_msleep(SENSOR_DELAY);
        float right_dist = hcsr04_get_distance();

        // 舵机回中
        sg90_set_angle(SERVO_CENTER);
        robot_mgr_update_servo_angle(SERVO_CENTER);
        osal_msleep(SERVO_DELAY);

        // 选择宽敞的一侧转向
        if (left_dist > right_dist)
            CAR_LEFT(); // 左侧宽敞，左转
        else
            CAR_RIGHT(); // 右侧宽敞，右转

        osal_msleep(TURN_TIME);
        CAR_STOP();
    } else
        CAR_FORWARD(); // 路径通畅，继续直行
}

/**
 * @brief 避障模式退出函数
 */
void mode_obstacle_exit(void)
{
    CAR_STOP();
    // 退出时舵机回中
    sg90_set_angle(SERVO_CENTER);
    robot_mgr_update_servo_angle(SERVO_CENTER);
    osal_msleep(SERVO_DELAY);
}
