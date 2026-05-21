#include "motor_executor.h"

#include <stdio.h>

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "robot_config.h"
#include "soc_osal.h"

#define MOTOR_EXEC_TIMEOUT_MS  400
#define MOTOR_EXEC_STACK_SIZE  2048
#define MOTOR_EXEC_PRIO        10

static unsigned long g_motor_queue = 0;
static osal_task *g_motor_task = NULL;

static int motor_executor_task(void *arg)
{
    (void)arg;
    MotorCmdMsg cmd;
    unsigned int size;

    printf("[Motor] Executor 任务启动\r\n");

    while (1) {
        size = sizeof(cmd);
        int ret = osal_msg_queue_read_copy(g_motor_queue, &cmd, &size,
                                           osal_msecs_to_jiffies(MOTOR_EXEC_TIMEOUT_MS));
        if (ret == OSAL_SUCCESS) {
            l9110s_set_differential(cmd.left, cmd.right);
        } else {
            l9110s_set_differential(0, 0);
        }
    }
    return 0;
}

void motor_executor_init(void)
{
    if (g_motor_queue != 0) return;
    // 队列长度设为 1：电机命令只需保留最新的，旧命令到达时立即过期。
    if (osal_msg_queue_create("motor_q", 1, &g_motor_queue, 0,
                              sizeof(MotorCmdMsg)) != OSAL_SUCCESS) {
        printf("[Motor] 队列创建失败\r\n");
        return;
    }

    osal_kthread_lock();
    g_motor_task = osal_kthread_create((osal_kthread_handler)motor_executor_task,
                                        NULL, "motor_exec", MOTOR_EXEC_STACK_SIZE);
    if (g_motor_task != NULL) {
        osal_kthread_set_priority(g_motor_task, MOTOR_EXEC_PRIO);
    }
    osal_kthread_unlock();

    if (g_motor_task != NULL) {
        printf("[Motor] Executor 初始化完成\r\n");
    } else {
        printf("[Motor] Executor 任务创建失败\r\n");
    }
}

bool motor_executor_push_cmd(int8_t left, int8_t right)
{
    if (g_motor_queue == 0) return false;

    if (left < -100) left = -100;
    if (left > 100) left = 100;
    if (right < -100) right = -100;
    if (right > 100) right = 100;

    MotorCmdMsg msg = {left, right};

    unsigned int msg_num = osal_msg_queue_get_msg_num(g_motor_queue);
    if (msg_num > 0) {
        MotorCmdMsg dummy;
        unsigned int sz = sizeof(dummy);
        osal_msg_queue_read_copy(g_motor_queue, &dummy, &sz, OSAL_MSGQ_NO_WAIT);
    }

    int ret = osal_msg_queue_write_copy(g_motor_queue, &msg, sizeof(msg), OSAL_MSGQ_NO_WAIT);
    return (ret == OSAL_SUCCESS);
}
