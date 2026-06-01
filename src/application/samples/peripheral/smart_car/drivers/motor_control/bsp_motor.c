#include "bsp_motor.h"
#include <stdio.h>
#include "../l9110s/bsp_l9110s.h"
#include "../../apps/car_demo/car_common.h"
#include "soc_osal.h"
#include "osal_timer.h"

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

static unsigned long g_motor_queue = 0;
static osal_task *g_motor_task = NULL;
static osal_timer g_motor_timer; // 超时定时

static void motor_timeout_callback(unsigned long arg)
{
    (void)arg;
    l9110s_set_differential(0, 0);
    osal_timer_stop(&g_motor_timer);
}

static int motor_executor_task(void *arg)
{
    (void)arg;
    int8_t speed[2];
    unsigned int size;
    int8_t last_l = 0, last_r = 0;

    printf("[Motor] Executor 任务启动\r\n");

    while (1) {
        size = sizeof(speed);
        int ret = osal_msg_queue_read_copy(g_motor_queue, &speed, &size, OSAL_WAIT_FOREVER);
        if (ret == OSAL_SUCCESS) {
            l9110s_set_differential(speed[0], speed[1]);
            // 仅在边沿打印（避免遥控时每包刷屏）
            if ((speed[0] == 0 && speed[1] == 0) != (last_l == 0 && last_r == 0))
                printf("[Motor] %d,%d\r\n", speed[0], speed[1]);

            last_l = speed[0];
            last_r = speed[1];

            osal_timer_stop(&g_motor_timer);
            osal_timer_start(&g_motor_timer);
        }
    }
    return 0;
}

// 初始化电机控制模块：创建消息队列、超时定时器和执行任务
void bsp_motor_init(void)
{
    if (g_motor_queue != 0)
        return;

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
    int8_t speed[2] = {CLAMP(left, -100, 100), CLAMP(right, -100, 100)};

    int ret = osal_msgq_overwrite(g_motor_queue, 1, &speed, sizeof(speed));
    return (ret == OSAL_SUCCESS);
}