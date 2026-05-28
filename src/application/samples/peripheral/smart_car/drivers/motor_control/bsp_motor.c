#include "bsp_motor.h"

#include <stdio.h>

#include "../l9110s/bsp_l9110s.h"
#include "../../apps/robot_demo/robot_common.h"
#include "soc_osal.h"
#include "osal_timer.h"

#define MOTOR_EXEC_TIMEOUT_MS 400  // 电机安全超时时间(ms)
#define MOTOR_EXEC_STACK_SIZE 2048 // 电机任务栈大小
#define MOTOR_EXEC_PRIO 10         // 电机任务优先级

static unsigned long g_motor_queue = 0;
static osal_task *g_motor_task = NULL;
static osal_timer g_motor_timer; // 电机安全超时定时器

/* 电机安全超时回调：超时未收到新指令则紧急停车 */
static void motor_timeout_callback(unsigned long arg)
{
    (void)arg;
    l9110s_set_differential(0, 0);
    osal_timer_stop(&g_motor_timer);
}

/* 电机执行任务：从消息队列读取指令并驱动电机，同时管理超时定时器 */
static int motor_executor_task(void *arg)
{
    (void)arg;
    MotorCmdMsg cmd;
    unsigned int size;
    int8_t last_l = 0, last_r = 0;

    printf("[Motor] Executor 任务启动\r\n");

    while (1) {
        size = sizeof(cmd);
        int ret = osal_msg_queue_read_copy(g_motor_queue, &cmd, &size, OSAL_WAIT_FOREVER);
        if (ret == OSAL_SUCCESS) {
            l9110s_set_differential(cmd.left, cmd.right);
            // 仅在边沿打印（避免遥控时每包刷屏）
            if ((cmd.left == 0 && cmd.right == 0) != (last_l == 0 && last_r == 0)) {
                printf("[Motor] %d,%d\r\n", cmd.left, cmd.right);
            }
            last_l = cmd.left;
            last_r = cmd.right;
            osal_timer_stop(&g_motor_timer);
            osal_timer_start(&g_motor_timer);
        }
    }
    return 0;
}

/* 初始化电机控制模块：创建消息队列、超时定时器和执行任务 */
void bsp_motor_init(void)
{
    if (g_motor_queue != 0)
        return;

    if (osal_msg_queue_create("motor_q", 1, &g_motor_queue, 0, sizeof(MotorCmdMsg)) != OSAL_SUCCESS) {
        printf("[Motor] 队列创建失败\r\n");
        return;
    }

    g_motor_timer.interval = MOTOR_EXEC_TIMEOUT_MS;
    g_motor_timer.handler = motor_timeout_callback;
    g_motor_timer.data = 0;
    if (osal_timer_init(&g_motor_timer) != OSAL_SUCCESS) {
        printf("[Motor] 定时器初始化失败\r\n");
        return;
    }

    g_motor_task = robot_task_create_locked("motor_exec", (osal_kthread_handler)motor_executor_task, NULL,
                                            MOTOR_EXEC_STACK_SIZE, MOTOR_EXEC_PRIO);

    if (g_motor_task != NULL) {
        printf("[Motor] Executor 初始化完成\r\n");
    }
}

/* 推送电机指令到消息队列（覆盖写入），限幅 -100~100 */
bool bsp_motor_push_cmd(int8_t left, int8_t right)
{
    if (g_motor_queue == 0)
        return false;

    if (left < -100)
        left = -100;
    if (left > 100)
        left = 100;
    if (right < -100)
        right = -100;
    if (right > 100)
        right = 100;

    MotorCmdMsg msg = {left, right};
    int ret = osal_msgq_overwrite(g_motor_queue, 1, &msg, sizeof(msg));
    return (ret == OSAL_SUCCESS);
}
