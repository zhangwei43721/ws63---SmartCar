/**
 ****************************************************************************************************
 * @file        step_motor_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS 步进电机实验
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
 * 实验现象：步进电机运行，K1控制方向，K2调速
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "app_init.h"

#define BLINKY_TASK_STACK_SIZE 0x1000
#define BLINKY_TASK_PRIO 24

// 定义步进电机速度，值越小，速度越快
// 最小不能小于1
#define STEPMOTOR_MAXSPEED        1  
#define STEPMOTOR_MINSPEED        5  

// key管脚定义
#define KEY1_PIN         11
#define KEY2_PIN         12

//使用宏定义独立按键按下的键值
#define KEY1_PRESS  1
#define KEY2_PRESS  2
#define KEY_UNPRESS 0 

//管脚定义
#define IN1_PIN         1
#define IN2_PIN         5
#define IN3_PIN         6
#define IN4_PIN         7

// 设置GPIO输出高低电平
#define MOTOR_IN1(a)          uapi_gpio_set_val(IN1_PIN,a)
#define MOTOR_IN2(a)          uapi_gpio_set_val(IN2_PIN,a)
#define MOTOR_IN3(a)          uapi_gpio_set_val(IN3_PIN,a)
#define MOTOR_IN4(a)          uapi_gpio_set_val(IN4_PIN,a)

// 函数声明
void step_motor_init(void);
void step_motor_run(uint8_t step,uint8_t dir,uint8_t speed,uint16_t angle,uint8_t sta);
uint8_t key_scan(void);
void key_init(void);

void key_init(void)
{
    uapi_pin_set_mode(KEY1_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(KEY1_PIN, GPIO_DIRECTION_INPUT);
    // 下降沿触发中断
    errcode_t ret = uapi_gpio_register_isr_func(KEY1_PIN, GPIO_INTERRUPT_FALLING_EDGE, NULL);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(KEY1_PIN);
    }

    uapi_pin_set_mode(KEY2_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(KEY2_PIN, GPIO_DIRECTION_INPUT);
    // 下降沿触发中断
    ret = uapi_gpio_register_isr_func(KEY2_PIN, GPIO_INTERRUPT_FALLING_EDGE, NULL);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(KEY2_PIN);
    }

}

/*******************************************************************************
* 函 数 名       : key_scan
* 函数功能        : 检测独立按键是否按下，按下则返回对应键值
* 输    入       : mode=0：单次扫描按键
                  mode=1：连续扫描按键
* 输    出       : KEY1_PRESS：K1按下
                  KEY2_PRESS：K2按下
                  KEY_UNPRESS：未有按键按下
*******************************************************************************/
uint8_t key_scan(void)
{
    static uint8_t key1 = 0;
    static uint8_t key2 = 0;

    key1 = uapi_gpio_get_val(KEY1_PIN);
    key2 = uapi_gpio_get_val(KEY2_PIN);

    if(key1 == 0 || key2 == 0)
    {
        osal_msleep(10); // 消抖
        key1 = uapi_gpio_get_val(KEY1_PIN);
        key2 = uapi_gpio_get_val(KEY2_PIN);
        if(key1==0 || key2==0)
        {
            if(key1==0) return KEY1_PRESS;
            else if(key2==0) return KEY2_PRESS;
        }

    }

    return KEY_UNPRESS;
}

// 步进电机初始化
void step_motor_init(void)
{
    // 设置引脚复用模式--HAL_PIO_FUNC_GPIO表示GPIO模式
    uapi_pin_set_mode(IN1_PIN, HAL_PIO_FUNC_GPIO);
    // 上拉模式
    uapi_pin_set_pull(IN1_PIN, PIN_PULL_TYPE_UP);
    // 输出模式
    uapi_gpio_set_dir(IN1_PIN, GPIO_DIRECTION_OUTPUT);

    // 设置引脚复用模式--HAL_PIO_FUNC_GPIO表示GPIO模式
    uapi_pin_set_mode(IN2_PIN, HAL_PIO_FUNC_GPIO);
    // 上拉模式
    uapi_pin_set_pull(IN2_PIN, PIN_PULL_TYPE_UP);
    // 输出模式
    uapi_gpio_set_dir(IN2_PIN, GPIO_DIRECTION_OUTPUT);

    // 设置引脚复用模式--HAL_PIO_FUNC_GPIO表示GPIO模式
    uapi_pin_set_mode(IN3_PIN, HAL_PIO_FUNC_GPIO);
    // 上拉模式
    uapi_pin_set_pull(IN3_PIN, PIN_PULL_TYPE_UP);
    // 输出模式
    uapi_gpio_set_dir(IN3_PIN, GPIO_DIRECTION_OUTPUT);

    // 设置引脚复用模式--HAL_PIO_FUNC_GPIO表示GPIO模式
    uapi_pin_set_mode(IN4_PIN, HAL_PIO_FUNC_GPIO);
    // 上拉模式
    uapi_pin_set_pull(IN4_PIN, PIN_PULL_TYPE_UP);
    // 输出模式
    uapi_gpio_set_dir(IN4_PIN, GPIO_DIRECTION_OUTPUT);

}

/*******************************************************************************
* 函 数 名       : step_motor_run
* 函数功能		 : 输出一个数据给ULN2003从而实现向步进电机发送一个脉冲
	    		   步进角为5.625/64度，如果需要转动一圈，那么需要360/5.625*64=4096个脉冲信号，
				   假如需要转动90度，需要的脉冲数=90*4096/360=1024脉冲信号。
				   如果任意角度对应多少脉冲呢？脉冲=angle*64/(8*5.625)
				   正好对应下面计算公式
				   (64*angle/45)*8，这个angle就是转动90度，45表示5.625*8，8代表8个脉冲，for循环。
* 输    入       : step：指定步进控制节拍，可选值4或8
				   dir：方向选择,1：顺时针,0：逆时针
				   speed：速度1-5
				   angle：角度选择0-360
				   sta：电机运行状态，1启动，0停止
* 输    出    	 : 无
*******************************************************************************/
void step_motor_run(uint8_t step,uint8_t dir,uint8_t speed,uint16_t angle,uint8_t sta)
{
	char i=0;
	uint16_t j=0;

	if(sta==1)
	{
		if(dir==0)	//如果为逆时针旋转
		{
			for(j=0;j<64*angle/45;j++) 
			{
				for(i=0;i<8;i+=(8/step))
				{
					switch(i)//8个节拍控制：A->AB->B->BC->C->CD->D->DA
					{
						case 0: MOTOR_IN1(1);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(0);break;
						case 1: MOTOR_IN1(1);MOTOR_IN2(1);MOTOR_IN3(0);MOTOR_IN4(0);break;
						case 2: MOTOR_IN1(0);MOTOR_IN2(1);MOTOR_IN3(0);MOTOR_IN4(0);break;
						case 3: MOTOR_IN1(0);MOTOR_IN2(1);MOTOR_IN3(1);MOTOR_IN4(0);break;
						case 4: MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(1);MOTOR_IN4(0);break;
						case 5: MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(1);MOTOR_IN4(1);break;
						case 6: MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(1);break;
						case 7: MOTOR_IN1(1);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(1);break;	
					}
					osal_msleep(speed);		
				}	
			}
		}
		else	//如果为顺时针旋转
		{
			for(j=0;j<64*angle/45;j++)
			{
				for(i=0;i<8;i+=(8/step))
				{
					switch(i)//8个节拍控制：A->AB->B->BC->C->CD->D->DA
					{
						case 0: MOTOR_IN1(1);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(1);break;
						case 1: MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(1);break;
						case 2: MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(1);MOTOR_IN4(1);break;
						case 3: MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(1);MOTOR_IN4(0);break;
						case 4: MOTOR_IN1(0);MOTOR_IN2(1);MOTOR_IN3(1);MOTOR_IN4(0);break;
						case 5: MOTOR_IN1(0);MOTOR_IN2(1);MOTOR_IN3(0);MOTOR_IN4(0);break;
						case 6: MOTOR_IN1(1);MOTOR_IN2(1);MOTOR_IN3(0);MOTOR_IN4(0);break;
						case 7: MOTOR_IN1(1);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(0);break;	
					}
					osal_msleep(speed);		
				}	
			}	
		}		
	}
	else
	{
		MOTOR_IN1(0);MOTOR_IN2(0);MOTOR_IN3(0);MOTOR_IN4(0);	
	}		
}


static void *step_motor_task(const char *arg)
{
    unused(arg);

    uint8_t key=0;
    uint8_t dir=0;
	uint8_t speed=STEPMOTOR_MAXSPEED;

    step_motor_init();//步进电机初始化
    key_init();//按键初始化

    while (1)
    {
        key=key_scan();//按键扫描
        if(key==KEY1_PRESS)//方向
            dir=!dir;
        else if(key==KEY2_PRESS)//加速
        {
			speed--;
			if(speed<STEPMOTOR_MAXSPEED)speed=STEPMOTOR_MINSPEED;
		}
        
        step_motor_run(8,dir,speed,1,1);
        
        osal_msleep(10);
    }
    return NULL;
}

static void step_motor_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务
    taskid = osal_kthread_create((osal_kthread_handler)step_motor_task, NULL, "step_motor_task", BLINKY_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, BLINKY_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(step_motor_entry);