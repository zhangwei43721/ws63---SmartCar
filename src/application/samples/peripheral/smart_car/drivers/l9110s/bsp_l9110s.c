/**
 ****************************************************************************************************
 * @file        bsp_l9110s.c
 * @author      SkyForever
 * @version     V1.2 (Fix Conflict & Direction)
 * @date        2025-01-12
 * @brief       L9110S电机驱动BSP层实现
 ****************************************************************************************************
 */

#include "bsp_l9110s.h"
#include "gpio.h"
#include "pinctrl.h"
#include "hal_gpio.h"
#include "soc_osal.h"

// 添加差速控制的软件PWM参数
#define MOTOR_PWM_PERIOD_US 20000 // 20ms周期 (50Hz)

// 全局变量用于差速控制
static volatile int8_t g_left_motor_speed = 0;
static volatile int8_t g_right_motor_speed = 0;
static volatile int g_diff_running = 0;

/**
 * @brief GPIO控制函数
 */
static void gpio_control(unsigned int gpio, unsigned int value)
{
    uapi_gpio_set_dir(gpio, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(gpio, value);
}

/**
 * @brief 微秒级延时
 */
static void delay_us(unsigned int duration_us)
{
    if (duration_us == 0) {
        return;
    }

    if (duration_us >= 1000) {
        osal_msleep(duration_us / 1000);
        duration_us %= 1000;
    }

    if (duration_us > 0) {
        osal_udelay(duration_us);
    }
}

/**
 * @brief 差速控制后台守护任务
 * @note  这是唯一控制GPIO的地方，避免冲突
 */
static void *motor_diff_daemon_task(const char *arg)
{
    (void)arg;
    while (g_diff_running) {
        int8_t left_speed = g_left_motor_speed;
        int8_t right_speed = g_right_motor_speed;

        unsigned int left_abs = (left_speed < 0) ? (unsigned int)(-left_speed) : (unsigned int)left_speed;
        unsigned int right_abs = (right_speed < 0) ? (unsigned int)(-right_speed) : (unsigned int)right_speed;

        // 限幅
        if (left_abs > 100)
            left_abs = 100;
        if (right_abs > 100)
            right_abs = 100;

        unsigned int left_on_us = (left_abs * MOTOR_PWM_PERIOD_US) / 100;
        unsigned int right_on_us = (right_abs * MOTOR_PWM_PERIOD_US) / 100;

        // 全停状态下休眠，节省CPU
        if (left_on_us == 0 && right_on_us == 0) {
            gpio_control(L9110S_LEFT_A_GPIO, 0);
            gpio_control(L9110S_LEFT_B_GPIO, 0);
            gpio_control(L9110S_RIGHT_A_GPIO, 0);
            gpio_control(L9110S_RIGHT_B_GPIO, 0);
            osal_msleep(20);
            continue;
        }

        // --- 开启阶段 (PWM High) ---

        // 左轮逻辑
        if (left_on_us > 0) {
            if (left_speed > 0) {
                // 左轮前进
                gpio_control(L9110S_LEFT_A_GPIO, 0);
                gpio_control(L9110S_LEFT_B_GPIO, 1);
            } else {
                // 左轮后退
                gpio_control(L9110S_LEFT_A_GPIO, 1);
                gpio_control(L9110S_LEFT_B_GPIO, 0);
            }
        } else {
            gpio_control(L9110S_LEFT_A_GPIO, 0);
            gpio_control(L9110S_LEFT_B_GPIO, 0);
        }

        // 右轮逻辑 (根据你的反馈：0/1是前进)
        if (right_on_us > 0) {
            if (right_speed > 0) {
                // 右轮前进 (A=0, B=1)
                gpio_control(L9110S_RIGHT_A_GPIO, 0);
                gpio_control(L9110S_RIGHT_B_GPIO, 1);
            } else {
                // 右轮后退 (A=1, B=0)
                gpio_control(L9110S_RIGHT_A_GPIO, 1);
                gpio_control(L9110S_RIGHT_B_GPIO, 0);
            }
        } else {
            gpio_control(L9110S_RIGHT_A_GPIO, 0);
            gpio_control(L9110S_RIGHT_B_GPIO, 0);
        }

        // --- PWM 延时控制 ---
        // 这里的逻辑是处理左右轮由于占空比不同而需要分段延时

        unsigned int first_end_us = left_on_us;
        if (right_on_us < first_end_us)
            first_end_us = right_on_us;

        if (first_end_us > 0)
            delay_us(first_end_us);

        // 第一阶段结束，关掉时间较短的那个
        if (left_on_us == first_end_us && left_on_us < MOTOR_PWM_PERIOD_US) {
            gpio_control(L9110S_LEFT_A_GPIO, 0);
            gpio_control(L9110S_LEFT_B_GPIO, 0);
        }
        if (right_on_us == first_end_us && right_on_us < MOTOR_PWM_PERIOD_US) {
            gpio_control(L9110S_RIGHT_A_GPIO, 0);
            gpio_control(L9110S_RIGHT_B_GPIO, 0);
        }

        unsigned int second_end_us = left_on_us;
        if (right_on_us > second_end_us)
            second_end_us = right_on_us;

        if (second_end_us > first_end_us)
            delay_us(second_end_us - first_end_us);

        // 第二阶段结束，关掉剩下的那个
        if (left_on_us == second_end_us && left_on_us < MOTOR_PWM_PERIOD_US) {
            gpio_control(L9110S_LEFT_A_GPIO, 0);
            gpio_control(L9110S_LEFT_B_GPIO, 0);
        }
        if (right_on_us == second_end_us && right_on_us < MOTOR_PWM_PERIOD_US) {
            gpio_control(L9110S_RIGHT_A_GPIO, 0);
            gpio_control(L9110S_RIGHT_B_GPIO, 0);
        }

        // 补齐剩余周期
        if (second_end_us < MOTOR_PWM_PERIOD_US)
            delay_us(MOTOR_PWM_PERIOD_US - second_end_us);
    }
    return NULL;
}

/**
 * @brief 初始化L9110S电机驱动
 */
void l9110s_init(void)
{
    uapi_pin_set_mode(L9110S_LEFT_A_GPIO, 2); // 注意复用信号2才是GPIO
    uapi_pin_set_mode(L9110S_LEFT_B_GPIO, 4); // 注意复用信号4才是GPIO
    uapi_pin_set_mode(L9110S_RIGHT_A_GPIO, 0);
    uapi_pin_set_mode(L9110S_RIGHT_B_GPIO, 0);

    uapi_gpio_set_dir(L9110S_LEFT_A_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(L9110S_LEFT_B_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(L9110S_RIGHT_A_GPIO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(L9110S_RIGHT_B_GPIO, GPIO_DIRECTION_OUTPUT);

    // 初始状态停止
    g_left_motor_speed = 0;
    g_right_motor_speed = 0;

    // 启动后台任务
    if (g_diff_running == 0) {
        g_diff_running = 1;
        osal_task *task =
            osal_kthread_create((osal_kthread_handler)motor_diff_daemon_task, NULL, "MotorDiffTask", 0x1000);
        if (task != NULL)
            osal_kthread_set_priority(task, 10);
    }
}

/**
 * @brief 设置双轮差速
 */
void l9110s_set_differential(int8_t left_speed, int8_t right_speed)
{
    g_left_motor_speed = left_speed;
    g_right_motor_speed = right_speed;
}
