#include "mode_obstacle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../robot_common.h"
#include "soc_osal.h"

// ================= 参数配置 =================
#define OBSTACLE_LIMIT 20.0f // 障碍物判定距离 (cm)
#define TIME_BACK_MS 300     // 后退一下的时间
#define TIME_TURN_90_MS 650  // 原地转90度所需时间
#define TIME_WAIT_STABLE 300 // 每次转完停顿检测的时间
#define TIME_BRAKE_MS 100    // 状态切换之间的刹车停顿

#define OBST_FWD_SPEED 60  // 前进电机速度 (-100~100)
#define OBST_BACK_SPEED 60 // 后退电机速度
#define OBST_TURN_SPEED 60 // 原地转弯速度

#define OBST_TICK_MS 20 // 主循环 tick（FORWARD/CHECKING 主动测距）
#define OBST_TASK_STACK_SIZE 2048
#define OBST_TASK_PRIO 22

#define OBST_EVENT_STOP 0x01  // 请求退出避障任务
#define OBST_EVENT_TIMER 0x02 // 定时器到期 → 推进状态机

// 避障状态机：obstacle_step() 每 OBST_TICK_MS 被调用，
// 定时阶段由 osal_timer 单次触发 OBST_EVENT_TIMER 推进
typedef enum {
    OBST_STATE_FORWARD = 0,       // 向前行驶，主动测距
    OBST_STATE_STOP_BEFORE_BACK,  // 刹车 100ms → 后退
    OBST_STATE_BACKING,           // 后退 300ms → 刹车
    OBST_STATE_STOP_BEFORE_TURN,  // 刹车 100ms → 转弯
    OBST_STATE_TURNING,           // 原地左转 650ms → 等待稳定
    OBST_STATE_STOP_BEFORE_CHECK, // 等 300ms 稳定 → 重新测距
    OBST_STATE_CHECKING,          // 读取 HC-SR04，通畅→FORWARD，受阻→TURNING
} obstacle_state_t;

static obstacle_state_t g_obst_state = OBST_STATE_FORWARD;

// 当前目标电机值，每个 tick 复推一次喂 motor_executor 400ms 看门狗
// （避障 TURNING/BACKING 等长时段 > 400ms，不喂狗电机会被中途强制停）
static int8_t g_cur_l = 0;
static int8_t g_cur_r = 0;

static osal_task *g_obst_task = NULL;
static osal_event g_obst_event;
static bool g_event_inited = false;

static osal_semaphore g_obst_exit_sem;
static bool g_exit_sem_inited = false;

static osal_timer g_obst_timer;
static bool g_obst_timer_inited = false;

/* 设置并推送电机速度命令，同时缓存当前值用于看门狗喂狗 */
static void obst_push(int8_t l, int8_t r)
{
    g_cur_l = l;
    g_cur_r = r;
    bsp_motor_push_cmd(l, r);
}

/* 避障定时器回调：触发TIMER事件推进状态机 */
static void obst_timer_cb(unsigned long arg)
{
    (void)arg;
    if (g_event_inited) {
        (void)osal_event_write(&g_obst_event, OBST_EVENT_TIMER);
    }
}

/* 重新配置并启动避障单次定时器 */
static void obst_arm_timer(uint32_t ms)
{
    if (!g_obst_timer_inited)
        return;
    // osal_timer_init 用的是 LOS_SWTMR_MODE_PERIOD（周期模式），且 LOS_SwtmrStart 用 init 时的 interval；
    // 单改 timer->interval 字段无效。用 osal_timer_mod 重建为 NO_SELFDELETE 单次模式并启动。
    osal_timer_mod(&g_obst_timer, ms);
}

/* 避障状态机单步推进：根据当前状态和定时器事件决定下一步动作 */
static void obstacle_step(bool timer_fired)
{
    switch (g_obst_state) {
        case OBST_STATE_FORWARD: {
            float d = hcsr04_get_distance();
            robot_mgr_update_distance(d);
            if (d > OBSTACLE_LIMIT) {
                obst_push(OBST_FWD_SPEED, OBST_FWD_SPEED);
            } else {
                printf("前方受阻(%dcm)，开始尝试寻找出口...\r\n", (int)d);
                obst_push(0, 0);
                g_obst_state = OBST_STATE_STOP_BEFORE_BACK;
                obst_arm_timer(TIME_BRAKE_MS);
            }
            break;
        }
        case OBST_STATE_STOP_BEFORE_BACK:
            if (timer_fired) {
                obst_push(-OBST_BACK_SPEED, -OBST_BACK_SPEED);
                g_obst_state = OBST_STATE_BACKING;
                obst_arm_timer(TIME_BACK_MS);
            }
            break;
        case OBST_STATE_BACKING:
            if (timer_fired) {
                obst_push(0, 0);
                g_obst_state = OBST_STATE_STOP_BEFORE_TURN;
                obst_arm_timer(TIME_BRAKE_MS);
            }
            break;
        case OBST_STATE_STOP_BEFORE_TURN:
            if (timer_fired) {
                obst_push(-OBST_TURN_SPEED, OBST_TURN_SPEED);
                g_obst_state = OBST_STATE_TURNING;
                obst_arm_timer(TIME_TURN_90_MS);
            }
            break;
        case OBST_STATE_TURNING:
            if (timer_fired) {
                obst_push(0, 0);
                g_obst_state = OBST_STATE_STOP_BEFORE_CHECK;
                obst_arm_timer(TIME_WAIT_STABLE);
            }
            break;
        case OBST_STATE_STOP_BEFORE_CHECK:
            if (timer_fired) {
                g_obst_state = OBST_STATE_CHECKING;
                // 立刻进入 CHECKING 主动测距，无需再等 timer
            }
            break;
        case OBST_STATE_CHECKING: {
            float d = hcsr04_get_distance();
            robot_mgr_update_distance(d);
            printf("转向后距离: %dcm\r\n", (int)d);
            if (d > OBSTACLE_LIMIT) {
                printf("找到出口！继续前进。\r\n");
                obst_push(OBST_FWD_SPEED, OBST_FWD_SPEED);
                g_obst_state = OBST_STATE_FORWARD;
            } else {
                printf("仍受阻，继续转向...\r\n");
                obst_push(-OBST_TURN_SPEED, OBST_TURN_SPEED);
                g_obst_state = OBST_STATE_TURNING;
                obst_arm_timer(TIME_TURN_90_MS);
            }
            break;
        }
    }
}

/* 避障任务入口：20ms周期轮询测距+喂看门狗，收到停止事件后退出 */
static int obstacle_task_entry(void *arg)
{
    (void)arg;
    printf("[Obstacle] 避障任务启动\r\n");

    while (1) {
        // 始终 20ms 唤醒：FORWARD/CHECKING 需要主动测距，其余状态需要喂 motor 400ms 看门狗
        // （TURNING=650ms > 400ms，不喂狗中途电机会被强停，看上去就是"抽搐"）
        int ret = osal_event_read(&g_obst_event, OBST_EVENT_STOP | OBST_EVENT_TIMER, OBST_TICK_MS,
                                  OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        if (ret > 0 && ((unsigned int)ret & OBST_EVENT_STOP)) {
            break;
        }
        bool timer_fired = (ret > 0 && ((unsigned int)ret & OBST_EVENT_TIMER));
        obstacle_step(timer_fired);

        // 复推当前目标速度，喂 motor_executor 看门狗
        if (g_cur_l != 0 || g_cur_r != 0) {
            bsp_motor_push_cmd(g_cur_l, g_cur_r);
        }
    }

    if (g_obst_timer_inited) {
        osal_timer_stop(&g_obst_timer);
    }
    bsp_motor_push_cmd(0, 0);
    printf("[Obstacle] 避障任务退出\r\n");
    if (g_exit_sem_inited) {
        osal_sem_up(&g_obst_exit_sem);
    }
    return 0;
}

/* 进入避障模式：初始化状态机、事件、定时器、创建任务 */
void mode_obstacle_enter(void)
{
    printf("进入智能避障模式\r\n");
    g_obst_state = OBST_STATE_FORWARD;
    g_cur_l = 0;
    g_cur_r = 0;
    bsp_motor_push_cmd(0, 0);

    if (!g_event_inited) {
        if (osal_event_init(&g_obst_event) == OSAL_SUCCESS) {
            g_event_inited = true;
        } else {
            printf("[Obstacle] 事件初始化失败\r\n");
            return;
        }
    }
    if (!g_exit_sem_inited) {
        osal_sem_binary_sem_init(&g_obst_exit_sem, 0);
        g_exit_sem_inited = true;
    }
    if (!g_obst_timer_inited) {
        g_obst_timer.interval = TIME_BRAKE_MS;
        g_obst_timer.handler = obst_timer_cb;
        g_obst_timer.data = 0;
        if (osal_timer_init(&g_obst_timer) == OSAL_SUCCESS) {
            g_obst_timer_inited = true;
        }
    }
    while (osal_sem_trydown(&g_obst_exit_sem) == OSAL_SUCCESS) {
    }

    if (g_obst_task != NULL)
        return;

    g_obst_task = robot_task_create_locked("obst_task", (osal_kthread_handler)obstacle_task_entry, NULL,
                                           OBST_TASK_STACK_SIZE, OBST_TASK_PRIO);
}

/* 退出避障模式：停止定时器、发送停止事件并等待任务退出 */
void mode_obstacle_exit(void)
{
    if (g_obst_task != NULL) {
        if (g_event_inited) {
            osal_event_write(&g_obst_event, OBST_EVENT_STOP);
        }
        if (g_exit_sem_inited) {
            (void)osal_sem_down_timeout(&g_obst_exit_sem, 500);
        }
        g_obst_task = NULL;
    }
    if (g_obst_timer_inited) {
        osal_timer_stop(&g_obst_timer);
    }
    bsp_motor_push_cmd(0, 0);
    g_obst_state = OBST_STATE_FORWARD;
}
