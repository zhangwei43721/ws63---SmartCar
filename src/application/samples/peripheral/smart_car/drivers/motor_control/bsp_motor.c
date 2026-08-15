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

// 电机命令队列元素：方向命令 或 左右轮差速 + 命令来源（决定看门狗语义）
typedef struct {
    bool is_dir;   // true=方向命令（dir/speed 有效）；false=差速命令（left/right 有效）
    uint8_t dir;   // CarDriveCmd 方向（is_dir 时有效）
    int8_t speed;  // 速度幅值（is_dir 时有效）
    int8_t left;   // 左轮差速（!is_dir 时有效）
    int8_t right;  // 右轮差速（!is_dir 时有效）
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

// 把抽象层"方向命令"语义转发给具体底盘驱动。
// 方向→物理动作（差速 or 电机+舵机）的翻译归各自底盘驱动所有。
static void drive_hardware_direction(uint8_t dir, int8_t speed)
{
#if USE_UART_SERVO_CHASSIS
    bsp_chassis_uart_drive(dir, speed);
#else
    l9110s_drive(dir, speed);
#endif
}

static int motor_executor_task(void *arg)
{
    (void)arg;
    int8_t cur_l = 0, cur_r = 0;
    int8_t last_l = 127, last_r = 127;
    bool cur_is_dir = false, last_is_dir = false;
    uint8_t cur_dir = 0, last_dir = 0;
    int8_t cur_speed = 0, last_speed = 0;
    motor_src_t last_src = MOTOR_SRC_AUTONOMOUS;
    unsigned int size;

    printf("[Motor] Executor 任务启动 (看门狗仅作用于遥控命令)\r\n");

    while (1) {
        motor_cmd_t cmd;
        size = sizeof(cmd);
        // 挂起等待新的控制指令（400ms 超时用于遥控看门狗防掉线）
        int ret = osal_msg_queue_read_copy(g_motor_queue, &cmd, &size, 400);
        if (ret == OSAL_SUCCESS) {
            last_src = cmd.src;
            if (cmd.is_dir) {
                cur_is_dir = true;
                cur_dir = cmd.dir;
                cur_speed = cmd.speed;
            } else {
                cur_is_dir = false;
                cur_l = cmd.left;
                cur_r = cmd.right;
            }
        } else if (last_src == MOTOR_SRC_MANUAL) {
            // 仅遥控命令超时停车；自主命令保持最后命令，靠模式 exit 显式停车
            cur_is_dir = true;
            cur_dir = CAR_DRIVE_STOP;
            cur_speed = 0;
        }

        // 仅在控制状态发生改变时发送一次帧给底层硬件；
        // 方向命令与差速命令之间切换也算"状态改变"，必须重发。
        bool changed;
        if (cur_is_dir) {
            changed = !last_is_dir || cur_dir != last_dir || cur_speed != last_speed;
        } else {
            changed = last_is_dir || cur_l != last_l || cur_r != last_r;
        }

        if (changed) {
            if (cur_is_dir) {
                drive_hardware_direction(cur_dir, cur_speed);
                printf("[Motor] State Changed -> Drive: dir=%u speed=%d\r\n", (unsigned)cur_dir, cur_speed);
            } else {
                drive_hardware_output(cur_l, cur_r);
                printf("[Motor] State Changed -> Drive: %d, %d\r\n", cur_l, cur_r);
            }
            last_is_dir = cur_is_dir;
            last_dir = cur_dir;
            last_speed = cur_speed;
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

    motor_cmd_t cmd = {
        .is_dir = false,
        .left = CLAMP(left, -100, 100),
        .right = CLAMP(right, -100, 100),
        .src = src,
    };
    int ret = osal_msgq_overwrite(g_motor_queue, 1, &cmd, sizeof(cmd));
    return (ret == OSAL_SUCCESS);
}

// 方向驾驶入口：遥控手动驾驶只上报方向，速度幅值由上层（car_ctrl）固定。
// 与 push_cmd 走同一队列 + 看门狗，源固定为遥控（MANUAL）。
void bsp_motor_drive(uint8_t dir, int8_t speed)
{
    if (g_motor_queue == 0)
        return;

    motor_cmd_t cmd = {
        .is_dir = true,
        .dir = dir,
        .speed = CLAMP(speed, -100, 100),
        .left = 0,
        .right = 0,
        .src = MOTOR_SRC_MANUAL,
    };
    (void)osal_msgq_overwrite(g_motor_queue, 1, &cmd, sizeof(cmd));
}
