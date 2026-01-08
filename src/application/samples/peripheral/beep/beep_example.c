/**
 ****************************************************************************************************
 * @file        mutex_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-28
 * @brief       LiteOS 蜂鸣器
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
 * 实验现象：蜂鸣器播放声音
 *
 ****************************************************************************************************
 */

#include "bsp_include/bsp_beep.h"

#define PWM_TASK_PRIO 24
#define PWM_TASK_STACK_SIZE 0x1000


static void *beep_task(const char *arg)
{
    UNUSED(arg);
    beep_init();
    beep_alarm(1000,1000);
    while(1)
    {
        uapi_tcxo_delay_us(10*100);
    }

    return NULL;
}

static void pwm_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)beep_task, 0, "PwmTask", PWM_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, PWM_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

/* Run the pwm_entry. */
app_run(pwm_entry);