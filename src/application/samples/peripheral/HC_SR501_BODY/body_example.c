/**
 ****************************************************************************************************
 * @file        body_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS
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
 * 实验现象：人体检测功能
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "watchdog.h"
#include "app_init.h"


#define BODY_GPIO 2 // 

#define BODY_TASK_STACK_SIZE 0x1000
#define BODY_TASK_PRIO 17

//使用宏定义独立按键按下的键值
#define BODY_PRESS  1
#define BODY_UNPRESS 0 

// 触发中断执行此函数
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    
    // g_ledState = !g_ledState;
    printf("有人经过.\r\n");
}


/*******************************************************************************
* 函 数 名       : body_scan
* 函数功能        : 检测是否有人经过
* 输    入       : void
                  
* 输    出       : 有人经过输出高电平，否则输入低电平
*******************************************************************************/
uint8_t body_scan(void)
{
    static uint8_t body = 0;


    body = uapi_gpio_get_val(BODY_GPIO);

    return body;
}

void body_init(void)
{
    uapi_pin_set_mode(BODY_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BODY_GPIO, GPIO_DIRECTION_INPUT);
    // 上升沿触发中断
    errcode_t ret = uapi_gpio_register_isr_func(BODY_GPIO, GPIO_INTERRUPT_HIGH, gpio_callback_func);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BODY_GPIO);
    }

}

static void *body_task(const char *arg)
{
    UNUSED(arg);
    body_init(); // 按键初始化

    while (1) {
        //uapi_watchdog_kick(); // 喂狗，防止程序出现异常系统挂死
        // if (g_ledState) {
        //     uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_HIGH);
        // } else {
        //     uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
        // }

        // uint8_t body = body_scan(); // 人体红外扫描
        // if(body == BODY_PRESS)
        //     printf("有人经过\n");
        // else
        //     //printf("无人经过\n");
        //     printf("body:%d\r\n",body);
        
        osal_msleep(1000);
    }

    return NULL;
}

static void body_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务1
    taskid = osal_kthread_create((osal_kthread_handler)body_task, NULL, "body_task", BODY_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, BODY_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(body_entry);