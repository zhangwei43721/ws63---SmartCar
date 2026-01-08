/**
 ****************************************************************************************************
 * @file        count_semaphore.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-24
 * @brief       LiteOS计数信号量
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
 * 实验现象：打开串口助手，任务1发送消息成功后，任务2接收消息；
 *
 ****************************************************************************************************
 */
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_init.h"
#include "common.h"
#include "soc_errno.h"

#define THREAD_TASK_STACK_SIZE 0x1000
#define THREAD_TASK_PRIO 24

#define SEM_MAX_COUNT 10      // 停车场最大容量
#define TASK1_DELAY_TIME 1000 // 进车频率
#define TASK2_DELAY_TIME 2000 // 出车频率

osal_task *task1_handle = NULL;
osal_task *task2_handle = NULL;

osal_semaphore sem_empty_slots; // 信号量：代表“空闲车位”

/// @brief 任务1 -- 进车 (消费者：消耗空车位)
static int thread_task1(const char *arg)
{
    unused(arg);
    int ret;

    while (1) {
        // 尝试进车：申请一个空车位 (等待 100ms)
        // 这里的逻辑是：如果 sem > 0，则减1并返回成功；如果 sem == 0，则等待。
        ret = osal_sem_down_timeout(&sem_empty_slots, 100);

        if (ret == OSAL_SUCCESS) {
            // 申请成功，意味着进车成功
            osal_printk("[Task 1] 进车成功! (消耗一个空位)\n");
        } else {
            // 申请失败，意味着没有空位了
            osal_printk("[Task 1] 进车失败! 停车场已满，请等待...\n");
        }

        osal_msleep(TASK1_DELAY_TIME);
    }

    return 0;
}

/// @brief 任务2 -- 出车 (生产者：产生空车位)
static int thread_task2(const char *arg)
{
    unused(arg);

    while (1) {
        // 出车：释放一个空车位
        // 这里的逻辑是：sem + 1
        osal_sem_up(&sem_empty_slots);
        osal_printk("[Task 2] 出去1辆车. (增加一个空位)\n");

        osal_msleep(TASK2_DELAY_TIME);
    }

    return 0;
}

void count_semaphore_entry(void)
{
    osal_printk("Litos计数信号量演示\r\n");

    // 创建信号量
    // 初始化值为 SEM_MAX_COUNT (10)，表示一开始有10个空位
    int ret = osal_sem_init(&sem_empty_slots, SEM_MAX_COUNT);
    if (ret == OSAL_FAILURE) {
        osal_printk("create sem failed\r\n");
        return;
    }
    osal_printk("ID = %d, create sem OK. 初始空位: %d\r\n", sem_empty_slots, SEM_MAX_COUNT);

    // 锁住任务，防止高优先级任务调度
    osal_kthread_lock();

    // 创建任务1 (进车)
    task1_handle = osal_kthread_create((osal_kthread_handler)thread_task1, NULL, "threadTask1", THREAD_TASK_STACK_SIZE);
    if (task1_handle != NULL) {
        osal_kthread_set_priority(task1_handle, THREAD_TASK_PRIO);
    }

    // 创建任务2 (出车)
    task2_handle = osal_kthread_create((osal_kthread_handler)thread_task2, NULL, "threadTask2", THREAD_TASK_STACK_SIZE);
    if (task2_handle != NULL) {
        osal_kthread_set_priority(task2_handle, THREAD_TASK_PRIO);
    }

    osal_kthread_unlock();
}

app_run(count_semaphore_entry);