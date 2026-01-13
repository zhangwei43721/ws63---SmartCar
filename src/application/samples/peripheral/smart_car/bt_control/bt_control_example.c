/**
 ****************************************************************************************************
 * @file        bt_control_example.c
 * @author      Smart Car Team
 * @version     V1.0
 * @date        2025-01-12
 * @brief       智能小车蓝牙SPP控制示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：通过蓝牙SPP接收控制命令控制小车运动
 *
 ****************************************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"

#include "bsp_l9110s.h"

#define BT_TASK_STACK_SIZE 0x4000
#define BT_TASK_PRIO 24

#define RECV_BUFFER_SIZE 128

// SPP UUID定义
#define SPP_UUID_CLASS 0x1101

/**
 * @brief 蓝牙连接状态
 */
typedef enum {
    BT_STATE_IDLE = 0,
    BT_STATE_SCANNING,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_DISCONNECTED
} bt_state_t;

static bt_state_t g_bt_state = BT_STATE_IDLE;
static int g_spp_fd = -1;

/**
 * @brief 处理接收到的控制命令
 * @param data 接收到的数据
 * @param len 数据长度
 * @return 无
 */
static void process_control_command(const char *data, size_t len)
{
    if (len == 0 || data == NULL) {
        return;
    }

    printf("BT Control: Received command: %s\n", data);

    // 解析命令并控制小车
    int command = atoi(data);

    switch (command) {
        case 0:  // 前进
            printf("Car: Forward\n");
            car_forward();
            break;
        case 1:  // 后退
            printf("Car: Backward\n");
            car_backward();
            break;
        case 2:  // 左转
            printf("Car: Left\n");
            car_left();
            break;
        case 3:  // 右转
            printf("Car: Right\n");
            car_right();
            break;
        case 4:  // 停止
            printf("Car: Stop\n");
            car_stop();
            break;
        default:
            // 支持字符命令
            if (len > 0) {
                switch (data[0]) {
                    case 'F':  // Forward
                    case 'f':
                        printf("Car: Forward\n");
                        car_forward();
                        break;
                    case 'B':  // Backward
                    case 'b':
                        printf("Car: Backward\n");
                        car_backward();
                        break;
                    case 'L':  // Left
                    case 'l':
                        printf("Car: Left\n");
                        car_left();
                        break;
                    case 'R':  // Right
                    case 'r':
                        printf("Car: Right\n");
                        car_right();
                        break;
                    case 'S':  // Stop
                    case 's':
                        printf("Car: Stop\n");
                        car_stop();
                        break;
                    default:
                        printf("Car: Unknown command: %c\n", data[0]);
                        break;
                }
            }
            break;
    }
}

/**
 * @brief 蓝牙SPP数据接收任务
 * @param arg 任务参数
 * @return NULL
 */
static void *bt_spp_recv_task(const char *arg)
{
    UNUSED(arg);
    char recv_buffer[RECV_BUFFER_SIZE];
    int retval;

    printf("BT SPP Recv Task: Start\n");

    while (g_bt_state == BT_STATE_CONNECTED) {
        memset(recv_buffer, 0, sizeof(recv_buffer));

        // 接收SPP数据
        // 注意: 这里需要根据实际的蓝牙SPP实现来调用相应的API
        // 以下是伪代码示例，需要根据WS63的蓝牙API进行调整

        /*
        retval = bt_spp_recv(g_spp_fd, recv_buffer, sizeof(recv_buffer) - 1);

        if (retval > 0) {
            process_control_command(recv_buffer, retval);
        } else if (retval == 0) {
            printf("BT SPP: Connection closed\n");
            break;
        } else {
            // 接收失败，可能是连接断开
            printf("BT SPP: Recv failed\n");
            break;
        }
        */

        // 临时模拟：使用串口接收
        osal_msleep(100);
    }

    // 连接断开，停止小车
    car_stop();
    g_bt_state = BT_STATE_DISCONNECTED;

    return NULL;
}

/**
 * @brief 蓝牙SPP服务器任务
 * @param arg 任务参数
 * @return NULL
 */
static void *bt_spp_server_task(const char *arg)
{
    UNUSED(arg);

    printf("BT SPP Server Task: Start\n");

    // 初始化蓝牙
    // 注意: 这里需要根据WS63的蓝牙API进行初始化
    // 以下是伪代码示例

    /*
    // 1. 使能蓝牙
    if (bt_enable() != 0) {
        printf("BT: Failed to enable\n");
        return NULL;
    }

    // 2. 设置蓝牙设备名称
    bt_set_device_name("WS63_SmartCar");

    // 3. 注册SPP服务
    if (bt_spp_register_service(SPP_UUID_CLASS) != 0) {
        printf("BT SPP: Failed to register service\n");
        return NULL;
    }

    // 4. 开始扫描
    g_bt_state = BT_STATE_SCANNING;
    */

    printf("BT SPP: Waiting for connection...\n");

    // 5. 等待SPP连接
    while (1) {
        if (g_bt_state == BT_STATE_IDLE) {
            /*
            // 接受SPP连接
            g_spp_fd = bt_spp_accept();
            if (g_spp_fd >= 0) {
                printf("BT SPP: Client connected\n");
                g_bt_state = BT_STATE_CONNECTED;

                // 创建接收任务
                osal_kthread_create((osal_kthread_handler)bt_spp_recv_task,
                                   NULL, "bt_spp_recv", BT_TASK_STACK_SIZE);
            }
            */
        }

        osal_msleep(1000);
    }

    return NULL;
}

/**
 * @brief 蓝牙控制任务
 * @param arg 任务参数
 * @return NULL
 */
static void *bt_control_task(const char *arg)
{
    UNUSED(arg);

    printf("Bluetooth Control Task: Start\n");

    // 初始化电机驱动
    l9110s_init();

    // 启动SPP服务器
    bt_spp_server_task(NULL);

    return NULL;
}

/**
 * @brief 蓝牙控制示例入口
 * @return 无
 */
static void bt_control_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("Bluetooth Control Example Entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)bt_control_task, NULL,
                                      "bt_control_task", BT_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, BT_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("BT Control: Failed to set task priority\n");
        }
    } else {
        printf("BT Control: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the bt_control_entry. */
app_run(bt_control_entry);
