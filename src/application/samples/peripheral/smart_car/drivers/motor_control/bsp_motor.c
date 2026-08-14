#include "bsp_motor.h"
#include <stdio.h>
#include "../../apps/car_demo/car_common.h"
#include "soc_osal.h"

#if defined(CONFIG_SMART_CAR_CHASSIS_TYPE_UART_SERVO) || defined(CONFIG_SMART_CAR_DRIVER_CHASSIS_UART)
#include "../chassis_uart/bsp_chassis_uart.h"
#define USE_UART_SERVO_CHASSIS 1
#else
#include "../l9110s/bsp_l9110s.h"
#define USE_UART_SERVO_CHASSIS 0
#endif

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

// 电机命令队列元素：左右轮速度 + 命令来源（决定看门狗语义）
typedef struct {
    int8_t left;
    int8_t right;
    motor_src_t src;
} motor_cmd_t;

static unsigned long g_motor_queue = 0;
static osal_task *g_motor_task = NULL;

// 把抽象层"左右轮差速"语义转发给具体底盘驱动。
// 底盘特有逻辑（L9110S 死区补偿、舵机转向运动学）均已下沉到各自驱动，
// 本层保持语义纯净：-100~100 差速。
static void drive_hardware_output(int8_t left, int8_t right)
{
#if USE_UART_SERVO_CHASSIS
    bsp_chassis_uart_set_differential(left, right);
#else
    l9110s_set_differential(left, right);
#endif
}

static int motor_executor_task(void *arg)
{
    (void)arg;
    int8_t cur_l = 0, cur_r = 0;
    int8_t last_l = 127, last_r = 127;
    motor_src_t last_src = MOTOR_SRC_AUTONOMOUS;
    unsigned int size;

    printf("[Motor] Executor 任务启动 (看门狗仅作用于遥控命令)\r\n");

    while (1) {
        motor_cmd_t cmd;
        size = sizeof(cmd);
        // 挂起等待新的控制指令（400ms 超时用于遥控看门狗防掉线）
        int ret = osal_msg_queue_read_copy(g_motor_queue, &cmd, &size, 400);
        if (ret == OSAL_SUCCESS) {
            cur_l = cmd.left;
            cur_r = cmd.right;
            last_src = cmd.src;
        } else if (last_src == MOTOR_SRC_MANUAL) {
            // 仅遥控命令超时停车；自主命令保持最后命令，靠模式 exit 显式停车
            cur_l = 0;
            cur_r = 0;
        }

        // 仅在控制状态发生改变时发送一次帧给底层硬件
        if (cur_l != last_l || cur_r != last_r) {
            drive_hardware_output(cur_l, cur_r);
            printf("[Motor] State Changed -> Drive: %d, %d\r\n", cur_l, cur_r);
            last_l = cur_l;
            last_r = cur_r;
        }
    }
    return 0;
}

// 初始化电机控制模块：创建消息队列和执行任务
void bsp_motor_init(void)
{
    if (g_motor_queue != 0)
        return;

#if USE_UART_SERVO_CHASSIS
    bsp_chassis_uart_init(NULL);
#else
    l9110s_init();
#endif

    if (osal_msg_queue_create("motor_q", 1, &g_motor_queue, 0, sizeof(motor_cmd_t)) != OSAL_SUCCESS) {
        printf("[Motor] 队列创建失败\r\n");
        return;
    }

    g_motor_task = car_task_create_locked("motor_exec", (osal_kthread_handler)motor_executor_task, NULL, 2048, 10);
    if (g_motor_task != NULL) {
        printf("[Motor] Executor 初始化完成\r\n");
    }
}

bool bsp_motor_push_cmd(int8_t left, int8_t right, motor_src_t src)
{
    if (g_motor_queue == 0)
        return false;

    // 入口限幅（死区补偿与底盘运动学已下沉到各底盘驱动，本层只做范围校验）
    motor_cmd_t cmd = {
        .left = CLAMP(left, -100, 100),
        .right = CLAMP(right, -100, 100),
        .src = src,
    };
    int ret = osal_msgq_overwrite(g_motor_queue, 1, &cmd, sizeof(cmd));
    return (ret == OSAL_SUCCESS);
}
