/**
 ****************************************************************************************************
 * @file        template.c
 * @author      jack
 * @version     V1.0
 * @date        2024-06-05
 * @brief       DHT11温度传感器实验
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
 * 实验现象：串口输出DHT11采集温湿度
 *
 ****************************************************************************************************
 */

#include "bsp_dht11/bsp_dht11.h"
#include "string.h"
#include "stdio.h"

#define DHT11_TASK_PRIO 24
#define DHT11_TASK_STACK_SIZE 0x1000


static void *dht11_task(const char *arg)
{
    UNUSED(arg);

    static uint8_t i=0;
    uint8_t temp;  	    
	uint8_t humi;
    
    while(dht11_init())
	{
		osal_printk("DHT11检测失败，请插好!\r\n");
		osal_msleep(500); //500ms
	}
	osal_printk("DHT11检测成功!\r\n");

    while(1)
    {
        i++;
        if(i%50==0)
        {
            dht11_read_data(&temp,&humi);
			printf("温度=%d°C  湿度=%d%%RH\r\n",temp,humi);
        }

        osal_msleep(10);
    }

    return NULL;
}

static void dht11_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)dht11_task, 0, "DHT11Task", DHT11_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, DHT11_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

/* Run the pwm_entry. */
app_run(dht11_entry);

