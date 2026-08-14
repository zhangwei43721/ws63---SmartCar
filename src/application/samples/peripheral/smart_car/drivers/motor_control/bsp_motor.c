#include "bsp_motor.h"
#include <stdio.h>
#include "../../apps/car_demo/car_common.h"
#include "soc_osal.h"
#include "osal_timer.h"

#if defined(CONFIG_SMART_CAR_CHASSIS_TYPE_UART_SERVO) || defined(CONFIG_SMART_CAR_DRIVER_CHASSIS_UART)
#include "../chassis_uart/bsp_chassis_uart.h"
#define USE_UART_SERVO_CHASSIS 1
#else
#include "../l9110s/bsp_l9110s.h"
#define USE_UART_SERVO_CHASSIS 0
#endif

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

static unsigned long g_motor_queue = 0;
static osal_task *g_motor_task = NULL;
static osal_timer g_motor_timer; // 超时定时

static void drive_hardware_output(int8_t left, int8_t right)
{
#if USE_UART_SERVO_CHASSIS
    // 转向舵机底盘算法:
    // 前后电机速度 = (left + right) / 2
    // 转向角度 = (right - left) * 3 放大转向摆幅增益 (上限 ±100)，使左右转时舵机能大角度打满舵
    int8_t motor_speed = (int8_t)CLAMP((left + right) / 2, -100, 100);
    int8_t steering    = (int8_t)CLAMP((right - left) * 3, -100, 100);
    int8_t servo1      = steering;
    int8_t servo2      = -steering;

    bsp_chassis_uart_send(motor_speed, servo1, servo2);
#else
    l9110s_set_differential(left, right);
#endif
}

static void motor_timeout_callback(unsigned long arg)
{
    (void)arg;
    drive_hardware_output(0, 0);
}

static int motor_executor_task(void *arg)
{
    (void)arg;
    int8_t speed[2] = {0, 0};
    int8_t cur_l = 0, cur_r = 0;
    int8_t last_l = 127, last_r = 127;
    unsigned int size;

    printf("[Motor] Executor 任务启动 (UART Chassis Mode: 单帧事件驱动锁存模式)\r\n");

    while (1) {
        size = sizeof(speed);
        // 挂起等待新的控制指令（带有 400ms 超时看门狗防掉线）
        int ret = osal_msg_queue_read_copy(g_motor_queue, &speed, &size, 400);
        if (ret == OSAL_SUCCESS) {
            cur_l = speed[0];
            cur_r = speed[1];
        } else {
            // 400ms 未收到新遥控指令，看门狗自动安全归零停车
            cur_l = 0;
            cur_r = 0;
        }

        // 仅在控制状态发生改变时发送一次 5 字节帧给底层 MCU
        if (cur_l != last_l || cur_r != last_r) {
            drive_hardware_output(cur_l, cur_r);
            printf("[Motor] State Changed -> Drive: %d, %d (sent 1 frame)\r\n", cur_l, cur_r);
            last_l = cur_l;
            last_r = cur_r;
        }

        if (cur_l == 0 && cur_r == 0) {
            osal_timer_stop(&g_motor_timer);
        } else {
            osal_timer_mod(&g_motor_timer, 400);
        }
    }
    return 0;
}

// 初始化电机控制模块：创建消息队列、超时定时器和执行任务
void bsp_motor_init(void)
{
    if (g_motor_queue != 0)
        return;

#if USE_UART_SERVO_CHASSIS
    bsp_chassis_uart_init(NULL);
#else
    l9110s_init();
#endif

    if (osal_msg_queue_create("motor_q", 1, &g_motor_queue, 0, 2) != OSAL_SUCCESS) {
        printf("[Motor] 队列创建失败\r\n");
        return;
    }

    g_motor_timer.interval = 400;
    g_motor_timer.handler = motor_timeout_callback;
    g_motor_timer.data = 0;
    if (osal_timer_init(&g_motor_timer) != OSAL_SUCCESS) {
        printf("[Motor] 定时器初始化失败\r\n");
        return;
    }

    g_motor_task = car_task_create_locked("motor_exec", (osal_kthread_handler)motor_executor_task, NULL, 2048, 10);
    if (g_motor_task != NULL) {
        printf("[Motor] Executor 初始化完成\r\n");
    }
}

bool bsp_motor_push_cmd(int8_t left, int8_t right)
{
    if (g_motor_queue == 0)
        return false;

    // 限幅
    int8_t cmd_l = CLAMP(left, -100, 100);
    int8_t cmd_r = CLAMP(right, -100, 100);

    // 线性死区补偿：将非零的 [1, 100] 线性映射到电机实际可克服阻力转动的 [50, 100]
    if (cmd_l > 0)
        cmd_l = 50 + (cmd_l * 50) / 100;
    else if (cmd_l < 0)
        cmd_l = -50 + (cmd_l * 50) / 100;

    if (cmd_r > 0)
        cmd_r = 50 + (cmd_r * 50) / 100;
    else if (cmd_r < 0)
        cmd_r = -50 + (cmd_r * 50) / 100;

    int8_t speed[2] = {cmd_l, cmd_r};
    int ret = osal_msgq_overwrite(g_motor_queue, 1, &speed, sizeof(speed));
    return (ret == OSAL_SUCCESS);
}