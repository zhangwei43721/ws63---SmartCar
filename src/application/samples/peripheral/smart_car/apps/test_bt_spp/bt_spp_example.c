/**
 ****************************************************************************************************
 * @file        bt_spp_example.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       蓝牙SPP示例
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：建立蓝牙SPP连接，接收并回显数据
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "../../drivers/bt_spp_server/bsp_bt_spp.h" // 引用封装好的头文件

#define BT_SPP_TASK_STACK_SIZE 0x1000
#define BT_SPP_TASK_PRIO 24
#define BT_DEVICE_NAME "WS63_UART" // 你的蓝牙名字

// 接收回调函数：手机发来的数据会到这里
static void spp_data_recived_handler(const uint8_t *data, uint32_t len)
{
    // 1. 打印收到的数据
    printf("[RX] Recv %d bytes: ", len);
    for (uint32_t i = 0; i < len; i++) {
        printf("%02X ", data[i]); // 打印16进制
    }
    printf("\r\n");

    // 打印字符形式方便调试
    printf("[RX] String: %.*s\r\n", (int)len, data);

    // 2. 数据回显 (Echo)：收到什么发回什么，测试双向通路
    bsp_bt_spp_send(data, len);
}

// 事件回调函数：连接状态变化会到这里
static void spp_event_handler(bsp_bt_spp_event_t event, void *data)
{
    UNUSED(data);
    switch (event) {
        case BSP_BT_SPP_EVENT_CONNECTED:
            printf(">>> [Event] 手机已连接! 请在APP中开启 Notify 接收数据 <<<\r\n");
            break;
        case BSP_BT_SPP_EVENT_DISCONNECTED:
            printf("<<< [Event] 手机已断开 <<<\r\n");
            break;
        default:
            break;
    }
}

static void *bt_spp_task(const char *arg)
{
    UNUSED(arg);
    int ret;
    int count = 0;
    char heartbeat_msg[32];

    printf("BT SPP Example: Start\r\n");

    // 1. 注册回调
    bsp_bt_spp_register_data_handler(spp_data_recived_handler);
    bsp_bt_spp_register_event_handler(spp_event_handler);

    // 2. 初始化蓝牙 (广播名：WS63_UART)
    ret = bsp_bt_spp_init(BT_DEVICE_NAME);
    if (ret != 0) {
        printf("BT Init Failed!\r\n");
        return NULL;
    }

    printf("BT Init Success. Waiting for connection...\r\n");

    // 3. 主循环
    while (1) {
        // 如果连接上了，每隔2秒发一个心跳包，方便你在手机上确认通了没
        if (bsp_bt_spp_get_status() == BSP_BT_SPP_STATUS_CONNECTED) {

            // 构造一个数据包
            snprintf(heartbeat_msg, sizeof(heartbeat_msg), "Heartbeat: %d", count++);

            // 发送数据
            ret = bsp_bt_spp_send((uint8_t *)heartbeat_msg, strlen(heartbeat_msg));

            if (ret > 0) {
                printf("[TX] Sent: %s\r\n", heartbeat_msg);
            } else {
                // 如果发送失败，通常是因为手机还没点“订阅(Notify)”
                // printf("[TX] Failed (Check if Notify enabled on Phone)\r\n");
            }
        }

        osal_msleep(2000); // 延时2秒
    }
    return NULL;
}

static void bt_spp_example_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)bt_spp_task, NULL, "bt_spp_task", BT_SPP_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, BT_SPP_TASK_PRIO);
    }
    osal_kthread_unlock();
}

app_run(bt_spp_example_entry);
