/**
 ****************************************************************************************************
 * @file        mutex_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-25
 * @brief       LiteOS事件
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
 * 实验现象：打开串口助手，任务1间隔1S设置一个事件标志，共设置3个事件，任务2等待接收完3个事件标志后执行
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_init.h"

#define THREAD_TASK_STACK_SIZE    0xc00
#define THREAD_TASK_PRIO          24
#define TASK_DELAY_TIME 1000 // 1s


osal_task *task1_handle = NULL;
osal_task *task2_handle = NULL;

#define EVENT1_FLAGES (1<<0)
#define EVENT2_FLAGES (1<<1)
#define EVENT3_FLAGES (1<<2)

// uint32_t event1_Flags = 0x00000001U;  // 事件掩码 每一位代表一个事件
// uint32_t event2_Flags = 0x00000002U;  // 事件掩码 每一位代表一个事件
// uint32_t event3_Flags = 0x00000004U;  // 事件掩码 每一位代表一个事件

osal_event event;


/// @brief 任务1--发送消息
/// @param arg 
/// @return 
static int thread_task1(const char *arg)
{
    unused(arg);

    while(1)
    {
        osal_printk("enter Task 1.......\n");
        int ret = osal_event_write(&event, EVENT1_FLAGES); // 设置事件标记 
        if(ret == OSAL_SUCCESS)   
            osal_printk("ret : %d\n",ret);
       
        osal_printk("send eventFlag1.......\n");

        osal_msleep(TASK_DELAY_TIME);

        osal_event_write(&event, EVENT2_FLAGES); // 设置事件标记        
        osal_printk("send eventFlag2.......\n");
        osal_msleep(TASK_DELAY_TIME);

        osal_event_write(&event, EVENT3_FLAGES); // 设置事件标记        
        osal_printk("send eventFlag3.......\n");
        osal_msleep(TASK_DELAY_TIME);
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
       int ret = osal_event_read(&event, EVENT1_FLAGES | EVENT2_FLAGES | EVENT3_FLAGES, OSAL_WAIT_FOREVER, OSAL_WAITMODE_OR);
       if(ret == OSAL_SUCCESS)
       {
            osal_printk("ret read : %d\n",ret);
       }
       printf("receive event is ok\n");

       // 清除标志位
        ret = osal_event_clear(&event, EVENT1_FLAGES | EVENT2_FLAGES | EVENT3_FLAGES);
        if (ret != OSAL_SUCCESS) {
            printf("event clear failed\r\n");
        }
        printf("example_task_event event clear success\r\n"); 

    }
   
    
    return 0;
}


void event_entry(void)
{
    osal_printk("Litos事件\r\n");

    // 创建事件
    int ret = osal_event_init(&event);
    if(ret == OSAL_FAILURE)
    {
        osal_printk("create event\r\n");
        return ;
    }
    osal_printk("ID = %d, create event OK\r\n", event);
    
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

app_run(event_entry);
