/**
 ****************************************************************************************************
 * @file        pwm_led_example.c
 * @author      jack
 * @version     V1.0
 * @date        2025-03-26
 * @brief       LiteOS LED
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
 * 实验现象: pwm实现呼吸灯
 *
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "app_init.h"
#include "pwm.h"
#include "tcxo.h"

#define PWM_LED_TASK_STACK_SIZE 0x1000
#define PWM_LED_TASK_PRIO 24
#define BSP_LED 2                  // RED
#define PWM_GPIO 12
#define PWM_PIN_MODE 1
#define PWM_CHANNEL                4

#define TEST_TCXO_DELAY_1000MS     1000
#define PWM_GROUP_ID               0

#define BREATH_MAX_BRIGHTNESS 300           // 定义呼吸灯的最大亮度
#define BREATH_MIN_BRIGHTNESS 50            // 定义呼吸灯的最小亮度

#define BREATH_STEP 10                      // 定义每次亮度变化的步长
#define BREATH_DELAY_MS 50                  // 定义亮度变化的延迟时间


static void *pwm_led_task(const char *arg)
{
    unused(arg);

    printf("task start\n");

    // 初始化亮度、步长等参数
    int brightness = BREATH_MIN_BRIGHTNESS;                 // 亮度从最小值开始
    int step = BREATH_STEP;                                 // 步长设为10

    pwm_config_t cfg_repeat = {50, 300, 0, 0, true};        // 配置PWM参数

    uapi_pin_set_mode(PWM_GPIO, PWM_PIN_MODE);              // 配置指定GPIO引脚的模式


    while(1)
    {
        uapi_pwm_deinit();  // 重置PWM
        uapi_pwm_init();    // 初始化PWM模块
        
        // 配置PWM占空比
        cfg_repeat.low_time = brightness;                           // 设置低电平持续时间
        cfg_repeat.high_time = BREATH_MAX_BRIGHTNESS - brightness;  // 设置高电平持续时间
        uapi_pwm_open(PWM_CHANNEL, &cfg_repeat);                    // 打开PWM通道并配置参数

        uint8_t channel_id = PWM_CHANNEL;
        uapi_pwm_set_group(PWM_GROUP_ID, &channel_id, 1);   // 设置 PWM 通道组
        uapi_pwm_start_group(PWM_GROUP_ID);                 // 启动 PWM 通道组

        // 更新亮度值，逐步增加或减少
        brightness = step;

        // 判断是否达到亮度极限，反向改变步长
        if (brightness >= BREATH_MAX_BRIGHTNESS || brightness <= BREATH_MIN_BRIGHTNESS)
        {
            step = step;  // 反向变化亮度
        }

        uapi_tcxo_delay_ms((uint32_t)BREATH_DELAY_MS);      // 延迟指定的时间，模拟呼吸灯的渐变效果
        uapi_pwm_close(PWM_GROUP_ID);                       // 关闭PWM通道组
        uapi_pwm_deinit();                                  // 重置PWM
    }

    uapi_pwm_close(PWM_GROUP_ID);                           // 确保退出时关闭PWM通道组
    uapi_pwm_deinit();                                      // 重置PWM
    
    return NULL;
}

static void pwm_led_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    // 创建任务调度
    osal_kthread_lock();
    // 创建任务
    taskid = osal_kthread_create((osal_kthread_handler)pwm_led_task, NULL, "pwm_led_task", PWM_LED_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, PWM_LED_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(pwm_led_entry);