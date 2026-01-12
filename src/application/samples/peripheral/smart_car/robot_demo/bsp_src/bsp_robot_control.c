/**
 ****************************************************************************************************
 * @file        bsp_robot_control.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       智能小车控制BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：智能小车循迹避障控制
 *
 ****************************************************************************************************
 */

#include "../bsp_include/bsp_robot_control.h"
#include "bsp_l9110s.h"
#include "bsp_hcsr04.h"
#include "bsp_tcrt5000.h"
#include "bsp_sg90.h"
#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include <stdio.h>

// 全局变量
static CarStatus g_car_status = CAR_STOP_STATUS;
static unsigned int g_car_speed_left = 0;
static unsigned int g_car_speed_right = 0;

/**
 * @brief 初始化智能小车控制系统
 * @return 无
 */
void robot_control_init(void)
{
    // 初始化电机驱动
    l9110s_init();

    // 初始化超声波传感器
    hcsr04_init();

    // 初始化红外传感器
    tcrt5000_init();

    // 初始化舵机
    sg90_init();

    printf("Robot control system initialized\n");
}

/**
 * @brief 获取小车当前状态
 * @return 小车状态
 */
CarStatus robot_get_status(void)
{
    return g_car_status;
}

/**
 * @brief 设置小车状态
 * @param status 小车状态
 * @return 无
 */
void robot_set_status(CarStatus status)
{
    g_car_status = status;
}

/**
 * @brief 舵机向左转动
 * @return 无
 */
static void engine_turn_left(void)
{
    sg90_set_angle(150);
}

/**
 * @brief 舵机向右转动
 * @return 无
 */
static void engine_turn_right(void)
{
    sg90_set_angle(30);
}

/**
 * @brief 舵机归中
 * @return 无
 */
static void regress_middle(void)
{
    sg90_set_angle(90);
}

/**
 * @brief 判断转向方向
 * @return CAR_TURN_LEFT 或 CAR_TURN_RIGHT
 */
static unsigned int engine_go_where(void)
{
    float left_distance = 0.0;
    float right_distance = 0.0;

    // 舵机向左转动测量左边障碍物距离
    engine_turn_left();
    osal_msleep(200);
    left_distance = hcsr04_get_distance();
    osal_msleep(100);

    // 归中
    regress_middle();
    osal_msleep(200);

    // 舵机向右转动测量右边障碍物距离
    engine_turn_right();
    osal_msleep(200);
    right_distance = hcsr04_get_distance();
    osal_msleep(100);

    // 归中
    regress_middle();

    printf("Left distance: %.2f cm, Right distance: %.2f cm\n", left_distance, right_distance);

    if (left_distance > right_distance) {
        return CAR_TURN_LEFT;
    } else {
        return CAR_TURN_RIGHT;
    }
}

/**
 * @brief 根据障碍物距离判断小车行走方向
 * @param distance 距离值 (cm)
 * @return 无
 */
static void car_where_to_go(float distance)
{
    if (distance < DISTANCE_BETWEEN_CAR_AND_OBSTACLE && distance > 0) {
        // 停止
        car_stop();
        osal_msleep(500);

        // 后退
        car_backward();
        osal_msleep(500);
        car_stop();

        // 判断转向方向
        unsigned int ret = engine_go_where();
        printf("Turning direction: %s\n", ret == CAR_TURN_LEFT ? "LEFT" : "RIGHT");

        if (ret == CAR_TURN_LEFT) {
            car_left();
            osal_msleep(500);
        } else {
            car_right();
            osal_msleep(500);
        }
        car_stop();
    } else {
        car_forward();
    }
}

/**
 * @brief 避障模式控制
 * @return 无
 */
void robot_obstacle_avoidance_mode(void)
{
    float distance = 0.0;

    regress_middle();

    while (g_car_status == CAR_OBSTACLE_AVOIDANCE_STATUS) {
        // 获取前方物体距离
        distance = hcsr04_get_distance();

        // 判断小车行走方向
        car_where_to_go(distance);

        osal_msleep(50);
    }

    // 退出时停止并归中
    car_stop();
    regress_middle();
}

/**
 * @brief 循迹模式控制
 * @return 无
 */
void robot_trace_mode(void)
{
    unsigned int left = 0;
    unsigned int right = 0;

    while (g_car_status == CAR_TRACE_STATUS) {
        // 获取左右传感器状态
        left = tcrt5000_get_left();
        right = tcrt5000_get_right();

        if (left == TCRT5000_ON_BLACK && right == TCRT5000_ON_BLACK) {
            // 两边都检测到黑线，前进
            car_forward();
        } else if (left == TCRT5000_ON_BLACK && right == TCRT5000_ON_WHITE) {
            // 左边检测到黑线，小车偏右，需要左转修正
            car_left();
        } else if (left == TCRT5000_ON_WHITE && right == TCRT5000_ON_BLACK) {
            // 右边检测到黑线，小车偏左，需要右转修正
            car_right();
        } else {
            // 都没有检测到黑线，前进
            car_forward();
        }

        osal_msleep(20);
    }

    // 退出时停止
    car_stop();
}
