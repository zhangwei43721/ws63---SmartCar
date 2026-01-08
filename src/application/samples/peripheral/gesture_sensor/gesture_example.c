/**
 ****************************************************************************************************
 * @file        gesture_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-24
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
 * 实验现象：手势识别
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
#include "PAJ7620/PAJ_I2c.h"
#include "PAJ7620/PAJ7620.h"

#define GESTURE_GPIO 11 // 

#define GESTURE_TASK_STACK_SIZE 0x1000
#define GESTURE_TASK_PRIO 17

#define TIME 500

static void *gesture_task(const char *arg)
{
    UNUSED(arg);
    I2C_Config();
    if(PAJ7620_Init()==false)
        printf("PAJ7620 initialize error.\r\n");
    //printf("PAJ7620 initialize sucess.\r\n");
    while (1) 
    {
        uint8_t data[2]={0,0};	
        IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);

        switch (data[0])
        {
        case GES_RIGHT_FLAG: // 右标志
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
			if(data[0] == GES_LEFT_FLAG) 
				printf("Right-Left\r\n"); // 从右到左
            else if(data[0] == GES_FORWARD_FLAG)
            {
                printf("Forward\r\n"); // 向前
                osal_msleep(500);
            }
            else if(data[0] == GES_BACKWARD_FLAG)
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
            else
                printf("Right\r\n"); // 右

        break;
        
        case GES_LEFT_FLAG: //左标志
            osal_msleep(TIME); 
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_RIGHT_FLAG) 
                printf("Left-Right\r\n"); // 从左到右
            else if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Forward\r\n"); // 向前
                osal_msleep(TIME);
            }
            else if(data[0] == GES_BACKWARD_FLAG) 
            {
                printf("Backward\r\n"); // 向后
                osal_msleep(TIME);
            }
            else
                printf("Left\r\n"); // 左          
        break;

        case GES_UP_FLAG: // 向上标志
            osal_msleep(TIME); 
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_DOWN_FLAG) 
                printf("Up-Down\r\n"); // 上下
            else if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Forward\r\n");
                osal_msleep(TIME);
            }
            else if(data[0] == GES_BACKWARD_FLAG) 
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
            else
                printf("Up\r\n"); // 上
        break;

        case GES_DOWN_FLAG: // 下标志
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_UP_FLAG) 
                printf("Down-Up\r\n");
            else if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Forward\r\n");
                osal_msleep(TIME);
            }
            else if(data[0] == GES_BACKWARD_FLAG) 
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
            else
                printf("Down\r\n");
        break;

        case GES_FORWARD_FLAG: // 获取向前标志
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_BACKWARD_FLAG)  // 向后标志
            {
                printf("Forward-Backward\r\n"); // 前后
                osal_msleep(TIME);
            }
            else
            {
                printf("Forward\r\n"); // 向前
                osal_msleep(TIME);
            }
        break;

        case GES_BACKWARD_FLAG:	// 向后倒退	  
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Backward-Forward\r\n"); // 向后向前
                osal_msleep(TIME);
            }
            else
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
        break;

        case GES_CLOCKWISE_FLAG: // 顺时针方向
				printf("Clockwise\r\n");
            break;
                
			case GES_COUNT_CLOCKWISE_FLAG:
				printf("anti-clockwise\r\n"); // 逆时针方向
            break;  
                
		default:
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x44, &data[1]);
            if (data[1] == GES_WAVE_FLAG) 
                printf("wave\r\n");
        break;

        }
        osal_msleep(100);
        uapi_watchdog_kick();
    }

    return NULL;
}

static void gesture_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务1
    taskid = osal_kthread_create((osal_kthread_handler)gesture_task, NULL, "body_task", GESTURE_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, GESTURE_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(gesture_entry);