/**
 ****************************************************************************************************
 * @file        thread_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-24
 * @brief       LiteOS任务管理
 * @license     
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:Hi3863
 * 在线视频:
 * 公司网址:
 * 购买地址:
 *
 ****************************************************************************************************
 * 实验现象：打开串口助手，任务2运行，挂起任务1，5秒后恢复任务1运行；
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_init.h"

#define THREAD_TASK_STACK_SIZE    0x1000
#define THREAD_TASK_PRIO          24

osal_task *task1_handle = NULL;
osal_task *task2_handle = NULL;

/// @brief 任务1
/// @param arg 
/// @return 
static int thread_task1(const char *arg)
{
    unused(arg);

    while(1)
    {
        osal_printk("Task 1...........\r\n");
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

    while(1)
    {
        osal_printk("Task 2, 开始挂起任务1\r\n");
        // 挂起任务1
        osal_kthread_suspend(task1_handle);
        osal_msleep(5000);

        osal_printk("Task 2, 开始恢复任务1\r\n");
        osal_kthread_resume(task1_handle);
        osal_msleep(5000);

    }
    
    return 0;
}

void thread_entry(void)
{
    
    // 锁住任务，防止高优先级任务调度
    osal_kthread_lock();
    // 创建任务1
    task1_handle = osal_kthread_create((osal_kthread_handler)thread_task1, NULL, "threadTask1", THREAD_TASK_STACK_SIZE);
    if(task1_handle != NULL) // 失败
    {
        // 设置优先级
        osal_kthread_set_priority(task1_handle, THREAD_TASK_PRIO);
        osal_kfree(task1_handle);
    }

    // 创建任务2
    task2_handle = osal_kthread_create((osal_kthread_handler)thread_task2, NULL, "threadTask2", THREAD_TASK_STACK_SIZE);
    if(task2_handle != NULL) // 失败
    {
        // 设置优先级
        osal_kthread_set_priority(task2_handle, THREAD_TASK_PRIO);
        osal_kfree(task2_handle);
    }

    osal_kthread_unlock();

    
}

app_run(thread_entry);