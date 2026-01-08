/**
 ****************************************************************************************************
 * @file        template.c
 * @author      jack
 * @version     V1.0
 * @date        2024-06-05
 * @brief       DS18B20温度传感器实验
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
 * 实验现象：串口输出DS1820采集温度
 *
 ****************************************************************************************************
 */

#include "bsp_ds18b20.h"
#include "string.h"
#include "stdio.h"

#define DS18B20_TASK_PRIO 24
#define DS18B20_TASK_STACK_SIZE 0x1000


static void *ds18b20_task(const char *arg)
{
    UNUSED(arg);

    static uint8_t i=0;
    uint16_t temper;
    
    while(ds18b20_init())
	{
		osal_printk("DS18B20检测失败，请插好!\r\n");
		osal_msleep(500); //500ms
	}
	osal_printk("DS18B20检测成功!\r\n");

    while (1) 
    {
        i++;
        if(i%50==0)
        {
            temper=ds18b20_gettemperture();
        
			if(temper<0)
			{
				osal_printk("检测的温度为：-");
			}
			else
			{
				osal_printk("检测的温度为： ");
			}
			osal_printk("%d\n",temper);
        }
        osal_msleep(10);
    }
   

    return NULL;
}

static void ds18b20_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)ds18b20_task, 0, "DS18B20Task", DS18B20_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, DS18B20_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

/* Run the pwm_entry. */
app_run(ds18b20_entry);

