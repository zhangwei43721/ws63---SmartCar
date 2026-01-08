/**
 ****************************************************************************************************
 * @file        message_queue_template.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-24
 * @brief       LiteOS消息队列
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

#define THREAD_TASK_STACK_SIZE    0x1000
#define THREAD_TASK_PRIO          24

#define SEM_MAX_COUNT 10
#define TASK1_DELAY_TIME 1000
#define TASK2_DELAY_TIME 2000

osal_task *task1_handle = NULL;
osal_task *task2_handle = NULL;

osal_semaphore semID;
uint8_t buff[20] = {0};       // 定义一个共享资源


/// @brief 任务1--发送消息
/// @param arg 
/// @return 
// static int thread_task1(const char *arg)
// {
//     unused(arg);

//     while(1)
//     {
//         osal_printk("[进入%d辆车, 停车场容量: %d] 信号量+1.\n", osal_Semaphore_getCount(semID), SEM_MAX_COUNT);
//         // 获取当前信号量值并判断是否小于最大信号量
//        if(osal_Semaphore_getCount(semID) < SEM_MAX_COUNT)
//        {
//             // 释放信号量 +1
//             osal_sem_up(&semID);
//             osal_printk("[进入%d辆车, 停车场容量: %d] 信号量+1.\n", osal_Semaphore_getCount(semID), SEM_MAX_COUNT);
//        }
//        else
//        {
//             osal_printk("[进入停车场失败, 请等待...]\n");
//        }
//         osal_msleep(TASK1_DELAY_TIME);
//     }
    
    
//     return 0;
// }


/// @brief 任务2
/// @param arg 
/// @return 
// static int thread_task2(const char *arg)
// {
//     unused(arg);
    
//     while (1) 
//     {
//         if(osal_sem_down_timeout(&semID, OSAL_WAIT_FOREVER) == OSAL_SUCCESS)
//         {
//             // 释放信号量 -1
//             printf("[出去1辆车, 剩余停车场容量: %d] 信号量-1.\n",  osal_Semaphore_getCount(semID));
//         }
//         else 
//         {
//             printf("[出停车场失败]\n");
//         }

//         osal_msleep(TASK2_DELAY_TIME);
//     }
   
    
//     return 0;
// }


void binary_semaphore_entry(void)
{
    osal_printk("Litos二值信号量\r\n");

    // 创建信号量
    int ret = osal_sem_init(&semID, SEM_MAX_COUNT-1);
    if(ret == OSAL_FAILURE)
    {
        osal_printk("create sem failed\r\n");
        return ;
    }
    osal_printk("ID = %d, create sem OK\r\n", semID);

    printf("[出去1辆车, 剩余停车场容量: %hd] 信号量-1.\n",  osal_Semaphore_getCount(&semID));

    
    // 锁住任务，防止高优先级任务调度
    osal_kthread_lock();
    // 创建任务1
    // task1_handle = osal_kthread_create((osal_kthread_handler)thread_task1, NULL, "threadTask1", THREAD_TASK_STACK_SIZE);
    // if(task1_handle != NULL) // 失败
    // {
    //     // 设置优先级
    //     osal_kthread_set_priority(task1_handle, THREAD_TASK_PRIO);
    //     osal_kfree(task1_handle);
    // }

    // // 创建任务2
    // task2_handle = osal_kthread_create((osal_kthread_handler)thread_task2, NULL, "threadTask2", THREAD_TASK_STACK_SIZE);
    // if(task2_handle != NULL) // 失败
    // {
    //     // 设置优先级
    //     osal_kthread_set_priority(task2_handle, THREAD_TASK_PRIO);
    //     osal_kfree(task2_handle);
    // }

    osal_kthread_unlock();



}

app_run(binary_semaphore_entry);