/**
 ****************************************************************************************************
 * @file        wifi_control_example.c
 * @author      Smart Car Team
 * @version     V1.0
 * @date        2025-01-12
 * @brief       智能小车WiFi TCP控制示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：通过WiFi TCP接收控制命令控制小车运动
 *
 ****************************************************************************************************
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "wifi_device.h"
#include "cmsis_os2.h"

#include "bsp_l9110s.h"

#define WIFI_TASK_STACK_SIZE 0x4000
#define WIFI_TASK_PRIO 24

#define TCP_SERVER_PORT 8888
#define RECV_BUFFER_SIZE 128

// WiFi连接配置
#define WIFI_SSID "BS-8"
#define WIFI_PASSWORD "BS88888888"

/**
 * @brief WiFi连接事件处理
 * @param event WiFi事件
 * @param data 事件数据
 * @return 无
 */
static void wifi_event_handler(wifi_event_t event, void *data)
{
    switch (event) {
        case WIFI_EVENT_STA_CONNECT:
            printf("WiFi: Connected to AP\n");
            break;
        case WIFI_EVENT_STA_DISCONNECT:
            printf("WiFi: Disconnected from AP\n");
            break;
        case WIFI_EVENT_SCAN_DONE:
            printf("WiFi: Scan done\n");
            break;
        default:
            break;
    }
}

/**
 * @brief 初始化并连接WiFi
 * @return 0成功，-1失败
 */
static int wifi_init_and_connect(void)
{
    int ret;
    wifi_device_config_t config = {0};

    printf("WiFi: Initializing...\n");

    // 注册事件回调
    ret = wifi_register_event_callback(wifi_event_handler);
    if (ret != 0) {
        printf("WiFi: Failed to register event callback\n");
        return -1;
    }

    // 使能STA模式
    ret = wifi_enable();
    if (ret != 0) {
        printf("WiFi: Failed to enable WiFi\n");
        return -1;
    }

    // 配置WiFi参数
    memcpy(config.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(config.pre_shared_key, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    config.security_type = WIFI_SEC_TYPE_PSK;

    // 连接AP
    printf("WiFi: Connecting to %s...\n", WIFI_SSID);
    ret = wifi_connect_ap(&config);
    if (ret != 0) {
        printf("WiFi: Failed to connect to AP\n");
        return -1;
    }

    printf("WiFi: Connection initiated\n");
    return 0;
}

/**
 * @brief TCP服务器任务
 * @param arg 任务参数
 * @return NULL
 */
static void *tcp_server_task(const char *arg)
{
    UNUSED(arg);
    int retval;
    int sockfd, connfd;
    struct sockaddr_in server_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    char recv_buffer[RECV_BUFFER_SIZE];

    printf("TCP Server: Starting on port %d\n", TCP_SERVER_PORT);

    // 创建TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("TCP Server: Failed to create socket\n");
        return NULL;
    }

    // 设置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 绑定端口
    retval = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (retval < 0) {
        printf("TCP Server: Failed to bind port, error=%d\n", retval);
        close(sockfd);
        return NULL;
    }
    printf("TCP Server: Bind to port %d success\n", TCP_SERVER_PORT);

    // 开始监听
    retval = listen(sockfd, 1);
    if (retval < 0) {
        printf("TCP Server: Failed to listen\n");
        close(sockfd);
        return NULL;
    }
    printf("TCP Server: Listening...\n");

    // 接受客户端连接
    while (1) {
        connfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (connfd < 0) {
            printf("TCP Server: Accept failed, error=%d\n", connfd);
            osal_msleep(1000);
            continue;
        }

        printf("TCP Server: Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        // 接收并处理客户端命令
        while (1) {
            memset(recv_buffer, 0, sizeof(recv_buffer));
            retval = recv(connfd, recv_buffer, sizeof(recv_buffer) - 1, 0);

            if (retval <= 0) {
                printf("TCP Server: Connection closed\n");
                car_stop();
                break;
            }

            printf("TCP Server: Received command: %s\n", recv_buffer);

            // 回显命令
            send(connfd, recv_buffer, strlen(recv_buffer), 0);

            // 解析命令并控制小车
            int command = atoi(recv_buffer);

            switch (command) {
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
                default:
                    printf("Car: Unknown command %d\n", command);
                    break;
            }
        }

        close(connfd);
    }

    close(sockfd);
    return NULL;
}

/**
 * @brief WiFi控制任务
 * @param arg 任务参数
 * @return NULL
 */
static void *wifi_control_task(const char *arg)
{
    UNUSED(arg);

    printf("WiFi Control Task: Start\n");

    // 初始化电机驱动
    l9110s_init();

    // 初始化并连接WiFi
    if (wifi_init_and_connect() != 0) {
        printf("WiFi Control Task: WiFi init failed\n");
        return NULL;
    }

    // 等待WiFi连接建立
    osal_msleep(5000);

    // 启动TCP服务器
    tcp_server_task(NULL);

    return NULL;
}

/**
 * @brief WiFi控制示例入口
 * @return 无
 */
static void wifi_control_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("WiFi Control Example Entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle =
        osal_kthread_create((osal_kthread_handler)wifi_control_task, NULL, "wifi_control_task", WIFI_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, WIFI_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("WiFi Control: Failed to set task priority\n");
        }
    } else {
        printf("WiFi Control: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the wifi_control_entry. */
app_run(wifi_control_entry);
