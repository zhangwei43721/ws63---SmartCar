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

#include "bsp_robot_control.h"
#include "../../drivers/l9110s/bsp_l9110s.h"
#include "../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../../drivers/sg90/bsp_sg90.h"
#include "../../drivers/wifi_client/bsp_wifi.h"
#include "../../drivers/bt_spp_server/bsp_bt_spp.h"
#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include <stdio.h>
#include <string.h>

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
    l9110s_init();   // 初始化电机驱动
    hcsr04_init();   // 初始化超声波传感器
    tcrt5000_init(); // 初始化红外传感器
    sg90_init();     // 初始化舵机

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

/**
 * @brief WiFi控制命令类型
 */
typedef enum {
    WIFI_CMD_FORWARD = 0,     // 前进
    WIFI_CMD_BACKWARD,        // 后退
    WIFI_CMD_LEFT,            // 左转
    WIFI_CMD_RIGHT,           // 右转
    WIFI_CMD_STOP,            // 停止
} wifi_cmd_t;

/**
 * @brief WiFi控制数据接收回调
 * @param data 接收到的数据
 * @param len 数据长度
 */
static void wifi_control_data_handler(const uint8_t *data, uint32_t len)
{
    if (len == 0 || data == NULL) {
        return;
    }

    uint8_t cmd = data[0];
    printf("WiFi control command received: %d\n", cmd);

    switch (cmd) {
        case WIFI_CMD_FORWARD:
            printf("Car: Forward\n");
            car_forward();
            break;
        case WIFI_CMD_BACKWARD:
            printf("Car: Backward\n");
            car_backward();
            break;
        case WIFI_CMD_LEFT:
            printf("Car: Left\n");
            car_left();
            break;
        case WIFI_CMD_RIGHT:
            printf("Car: Right\n");
            car_right();
            break;
        case WIFI_CMD_STOP:
            printf("Car: Stop\n");
            car_stop();
            break;
        default:
            printf("Unknown command: %d\n", cmd);
            break;
    }
}

/**
 * @brief WiFi远程控制模式
 * @return 无
 */
void robot_wifi_control_mode(void)
{
#if defined(CONFIG_SMART_CAR_WIFI_ENABLE) || defined(CONFIG_SMART_CAR_WIFI_EXAMPLE)
    char ip_str[32] = {0};
    bsp_wifi_status_t status;

    printf("Starting WiFi control mode...\n");

    // 初始化WiFi
    if (bsp_wifi_init() != 0) {
        printf("WiFi initialization failed\n");
        robot_set_status(CAR_STOP_STATUS);
        return;
    }

    // 连接到默认WiFi
    printf("Connecting to WiFi...\n");
    if (bsp_wifi_connect_default() != 0) {
        printf("WiFi connection failed\n");
        robot_set_status(CAR_STOP_STATUS);
        return;
    }

    // 等待获取IP地址
    printf("Waiting for IP address...\n");
    int retry = 0;
    while (retry < 30) {
        status = bsp_wifi_get_status();
        if (status == BSP_WIFI_STATUS_GOT_IP) {
            if (bsp_wifi_get_ip(ip_str, sizeof(ip_str)) == 0) {
                printf("WiFi connected! IP: %s\n", ip_str);
                printf("Please connect to %s:8888 to control the car\n", ip_str);
                break;
            }
        }
        osal_msleep(1000);
        retry++;
    }

    if (status != BSP_WIFI_STATUS_GOT_IP) {
        printf("Failed to get IP address\n");
        robot_set_status(CAR_STOP_STATUS);
        return;
    }

    // TODO: 创建TCP服务器监听8888端口
    // 注意: 此处为框架代码，需要根据WS63的TCP Socket API进行实现
    printf("WiFi control mode: Waiting for TCP connection on port 8888...\n");
    printf("Control commands: 0=Forward, 1=Backward, 2=Left, 3=Right, 4=Stop\n");

    while (g_car_status == CAR_WIFI_CONTROL_STATUS) {
        // TODO: 接收TCP数据并调用 wifi_control_data_handler
        // 这部分需要根据WS63的实际Socket API进行实现

        osal_msleep(100);
    }

    // 退出时断开WiFi并停止小车
    bsp_wifi_disconnect();
    car_stop();
    printf("WiFi control mode exited\n");
#else
    printf("WiFi module is not enabled. Please enable SMART_CAR_WIFI_ENABLE in menuconfig.\n");
    robot_set_status(CAR_STOP_STATUS);
#endif
}

/**
 * @brief 蓝牙控制数据接收回调
 * @param data 接收到的数据
 * @param len 数据长度
 */
static void bt_control_data_handler(const uint8_t *data, uint32_t len)
{
    if (len == 0 || data == NULL) {
        return;
    }

    char cmd = data[0];
    printf("Bluetooth control command received: %c\n", cmd);

    // 支持数字命令: 0-4
    if (cmd >= '0' && cmd <= '4') {
        switch (cmd - '0') {
            case 0: // 前进
                printf("Car: Forward\n");
                car_forward();
                break;
            case 1: // 后退
                printf("Car: Backward\n");
                car_backward();
                break;
            case 2: // 左转
                printf("Car: Left\n");
                car_left();
                break;
            case 3: // 右转
                printf("Car: Right\n");
                car_right();
                break;
            case 4: // 停止
                printf("Car: Stop\n");
                car_stop();
                break;
        }
    }
    // 支持字符命令: F/f, B/b, L/l, R/r, S/s
    else {
        switch (cmd) {
            case 'F':
            case 'f':
                printf("Car: Forward\n");
                car_forward();
                break;
            case 'B':
            case 'b':
                printf("Car: Backward\n");
                car_backward();
                break;
            case 'L':
            case 'l':
                printf("Car: Left\n");
                car_left();
                break;
            case 'R':
            case 'r':
                printf("Car: Right\n");
                car_right();
                break;
            case 'S':
            case 's':
                printf("Car: Stop\n");
                car_stop();
                break;
            default:
                printf("Unknown command: %c\n", cmd);
                break;
        }
    }
}

/**
 * @brief 蓝牙事件处理回调
 * @param event 蓝牙事件
 * @param data 事件数据
 */
static void bt_event_handler(bsp_bt_spp_event_t event, void *data)
{
    UNUSED(data);
    switch (event) {
        case BSP_BT_SPP_EVENT_CONNECTED:
            printf("Bluetooth SPP connected\n");
            break;
        case BSP_BT_SPP_EVENT_DISCONNECTED:
            printf("Bluetooth SPP disconnected\n");
            break;
        case BSP_BT_SPP_EVENT_DATA_RECEIVED:
            // 数据由 data_handler 处理
            break;
        default:
            break;
    }
}

/**
 * @brief 蓝牙远程控制模式
 * @return 无
 */
void robot_bt_control_mode(void)
{
#if defined(CONFIG_SMART_CAR_BT_SPP_ENABLE) || defined(CONFIG_SMART_CAR_BT_SPP_EXAMPLE)
    printf("Starting Bluetooth control mode...\n");

    // 初始化蓝牙SPP
    if (bsp_bt_spp_init("WS63_SmartCar") != 0) {
        printf("Bluetooth SPP initialization failed\n");
        robot_set_status(CAR_STOP_STATUS);
        return;
    }

    // 注册事件和数据处理回调
    bsp_bt_spp_register_event_handler(bt_event_handler);
    bsp_bt_spp_register_data_handler(bt_control_data_handler);

    printf("Bluetooth SPP initialized, device name: WS63_SmartCar\n");
    printf("Waiting for SPP connection...\n");

    // 等待SPP连接
    while (g_car_status == CAR_BT_CONTROL_STATUS) {
        bsp_bt_spp_status_t status = bsp_bt_spp_get_status();
        if (status == BSP_BT_SPP_STATUS_CONNECTED) {
            // 已连接，等待控制命令
            osal_msleep(100);
        } else if (status == BSP_BT_SPP_STATUS_IDLE) {
            // 等待连接
            osal_msleep(500);
        } else {
            // 连接断开或其他状态
            osal_msleep(500);
        }
    }

    // 退出时断开蓝牙并停止小车
    bsp_bt_spp_disconnect();
    car_stop();
    printf("Bluetooth control mode exited\n");
#else
    printf("Bluetooth SPP module is not enabled. Please enable SMART_CAR_BT_SPP_ENABLE in menuconfig.\n");
    robot_set_status(CAR_STOP_STATUS);
#endif
}
