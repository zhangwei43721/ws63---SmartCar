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
#include "../../drivers/bt_spp_server/bsp_bt_spp.h"

#define BT_SPP_TASK_STACK_SIZE 0x2000
#define BT_SPP_TASK_PRIO 24
#define BT_DEVICE_NAME "WS63_SmartCar"

/**
 * @brief SPP数据接收回调
 * @param data 接收到的数据
 * @param len 数据长度
 */
static void spp_data_handler(const uint8_t *data, uint32_t len)
{
    printf("BT SPP Example: Received %u bytes: ", len);
    for (uint32_t i = 0; i < len && i < 32; i++) {
        printf("%c", data[i]);
    }
    printf("\n");

    // 回显数据
    bsp_bt_spp_send(data, len);
}

/**
 * @brief SPP事件回调
 * @param event SPP事件
 * @param data 事件数据
 */
static void spp_event_handler(bsp_bt_spp_event_t event, void *data)
{
    switch (event) {
        case BSP_BT_SPP_EVENT_CONNECTED:
            printf("BT SPP Example: Client connected\n");
            break;
        case BSP_BT_SPP_EVENT_DISCONNECTED:
            printf("BT SPP Example: Client disconnected\n");
            break;
        case BSP_BT_SPP_EVENT_DATA_RECEIVED:
            // 数据已在data_handler中处理
            break;
        default:
            break;
    }
}

/**
 * @brief 蓝牙SPP测试任务
 * @param arg 任务参数
 * @return NULL
 */
static void *bt_spp_task(const char *arg)
{
    UNUSED(arg);
    int ret;

    printf("BT SPP Example: Start\n");

    // 注册回调
    bsp_bt_spp_register_data_handler(spp_data_handler);
    bsp_bt_spp_register_event_handler(spp_event_handler);

    // 初始化蓝牙SPP
    ret = bsp_bt_spp_init(BT_DEVICE_NAME);
    if (ret != 0) {
        printf("BT SPP Example: Failed to initialize BT SPP\n");
        return NULL;
    }

    // 等待连接
    ret = bsp_bt_spp_wait_connection(0);  // 0表示一直等待
    if (ret != 0) {
        printf("BT SPP Example: Failed to wait for connection\n");
        return NULL;
    }

    printf("BT SPP Example: Ready to receive data\n");

    // 持续运行
    while (1) {
        osal_msleep(1000);
    }

    return NULL;
}

/**
 * @brief 蓝牙SPP示例入口
 * @return 无
 */
static void bt_spp_example_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("BT SPP Example Entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)bt_spp_task, NULL,
                                      "bt_spp_task", BT_SPP_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, BT_SPP_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("BT SPP Example: Failed to set task priority\n");
        }
    } else {
        printf("BT SPP Example: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the bt_spp_example_entry. */
app_run(bt_spp_example_entry);
