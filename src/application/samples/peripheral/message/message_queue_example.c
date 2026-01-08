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

osal_task *task1_handle = NULL;
osal_task *task2_handle = NULL;

unsigned long MsgQueue_ID;

typedef struct message_people 
{
    uint8_t id;     // ID
    uint8_t age;    // 年龄
    char *name;     // 名字
}msg_people_t;
msg_people_t msg_people;


/// @brief 任务1--发送消息
/// @param arg 
/// @return 
static int thread_task1(const char *arg)
{
    unused(arg);
    int msgStatus;

    while(1)
    {
       // osal_printk("enter task 1......\n");
        msg_people.id = 0;
        msg_people.age = 16; // 年龄 16岁
        msg_people.name = "jack";
        msgStatus = osal_msg_queue_write_copy(MsgQueue_ID, &msg_people, sizeof(msg_people), 0);
        if (msgStatus == OSAL_SUCCESS)
        {
            //osal_printk("osal_msg_queue_write_copy is ok.\n");
        }

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
    
    int msgStatus;
    while(1)
    {
       // osal_printk("enter Task 2.......\r\n");
        unsigned int msg_buf_size = sizeof(msg_people);
        msgStatus = osal_msg_queue_read_copy(MsgQueue_ID, &msg_people, &msg_buf_size, 0);
        if (msgStatus == OSAL_SUCCESS)
        {
            osal_printk("osal_msg_queue_read_copy is ok.\n");
            osal_printk("recv id = %d, age = %d, name = %s\n", msg_people.id, msg_people.age, msg_people.name);
            break;
        }
        //osal_msleep(1000);
    }
    
    return 0;
}


void queue_entry(void)
{
    // 创建消息队列
    int ret = osal_msg_queue_create("MsgQueue", 16, &MsgQueue_ID, 0,sizeof(msg_people));
    if (ret == OSAL_SUCCESS)
    {
        osal_printk("ID = %d , create msgqueueID is ok!\n",MsgQueue_ID);
    }

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

app_run(queue_entry);