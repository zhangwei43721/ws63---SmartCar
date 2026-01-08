/**
 ****************************************************************************************************
 * @file        MyTimer_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS 定时器中断实验
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
 * 实验现象：定时点亮LED
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "app_init.h"

#define MYTIMER_TASK_STACK_SIZE 0x1000
#define MYTIMER_TASK_PRIO 24
#define BSP_LED 7                  // RED

void led_init(void)
{
    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
}

osal_timer timer;

// 时间达到，执行此回调函数
void timer_callback(unsigned long val)
{
    UNUSED(val);
    printf("timer callback\n");
    static uint8_t i=0;
    i=!i; 
    uapi_gpio_set_val(BSP_LED,i);
}

void timer_init(int time)
{
    // 设置定时器参数
    timer.interval = time;
    timer.handler = timer_callback; // 注册回调函数

    // 源码默认是单次定时，如果想周期性定时，则修改osal_timer_init源码 LOS_SWTMR_MODE_PERIOD
    int ret = osal_timer_init(&timer);
    if(ret != OSAL_SUCCESS)
    {
        printf("timer init failed\n");
        return;
    }

    osal_timer_start(&timer);

    printf("cycle : %d\n",osal_get_cycle_per_tick());
    
}

void time_onoff(uint8_t sta)
{
    if(sta==0)osal_timer_stop(&timer);
    else osal_timer_start(&timer);
}

static void *MyTimer_task(const char *arg)
{
    unused(arg);

    printf("task start\n");

    led_init();
    timer_init(500);

    while (1) {
        osal_msleep(10);
        
    }
    return NULL;
}

static void MyTimer_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务
    taskid = osal_kthread_create((osal_kthread_handler)MyTimer_task, NULL, "MyTimer_task", MYTIMER_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, MYTIMER_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(MyTimer_entry);