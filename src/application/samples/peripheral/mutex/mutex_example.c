/**
 ****************************************************************************************************
 * @file        mutex_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-25
 * @brief       LiteOS互斥锁
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:Hi3863
 * 在线视频:
 * 公司网址:
 * 购买地址:
 *
 ****************************************************************************************************
 * 实验现象：打开串口助手，任务1在写数据时，任务2不能读，任务2在读数据时，任务1不能写
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_init.h"

#define THREAD_TASK_STACK_SIZE 0x1000
#define THREAD_TASK_PRIO 24

osal_task *task1_handle = NULL;
osal_task *task2_handle = NULL;

osal_mutex mutex;
uint8_t buff[20] = {0}; // 定义一个共享资源

/// @brief 任务1--发送消息
/// @param arg
/// @return
static int thread_task1(const char *arg)
{
    unused(arg);
    while (1) {
        osal_printk("enter Task 1.......\n");
        // 加锁
        osal_mutex_lock_timeout(&mutex, OSAL_WAIT_FOREVER);

        int len = sizeof(buff);

        // 操作共享数据 写数据
        printf("write Buff Data: \n");
        for (int i = 0; i < len; i++) {
            buff[i] = i;
        }
        osal_printk("\n");

        // 释放互斥锁
        osal_mutex_unlock(&mutex);

        osal_msleep(1000);
    }

    return 0;
}

/// @brief 任务2
/// @param arg
/// @return
static int thread_task2(const char *arg)
{
    unused(arg);

    while (1) {
        osal_printk("enter Task 2.......\n");
        // 加锁
        osal_mutex_lock_timeout(&mutex, OSAL_WAIT_FOREVER);

        int len = sizeof(buff);

        // 操作共享数据 读数据
        osal_printk("read Buff Data: \n");
        for (int i = 0; i < len; i++) {
            osal_printk("%d \n", buff[i]);
        }
        osal_printk("\n");

        // 释放互斥锁
        osal_mutex_unlock(&mutex);

        osal_msleep(1000);
    }

    return 0;
}

void mutex_entry(void)
{
    osal_printk("Litos互斥锁\r\n");

    // 创建互斥锁
    int ret = osal_mutex_init(&mutex);
    if (ret == OSAL_FAILURE) {
        osal_printk("create mutex\r\n");
        return;
    }
    osal_printk("ID = %d, create mutex OK\r\n", mutex);

    // 锁住任务，防止高优先级任务调度
    osal_kthread_lock();
    // 创建任务1
    task1_handle = osal_kthread_create((osal_kthread_handler)thread_task1, NULL, "threadTask1", THREAD_TASK_STACK_SIZE);
    if (task1_handle != NULL) { // 失败
        // 设置优先级
        osal_kthread_set_priority(task1_handle, THREAD_TASK_PRIO);
        osal_kfree(task1_handle);
    }

    // 创建任务2
    task2_handle = osal_kthread_create((osal_kthread_handler)thread_task2, NULL, "threadTask2", THREAD_TASK_STACK_SIZE);
    if (task2_handle != NULL) { // 失败
        // 设置优先级
        osal_kthread_set_priority(task2_handle, THREAD_TASK_PRIO);
        osal_kfree(task2_handle);
    }

    osal_kthread_unlock();
}

app_run(mutex_entry);
