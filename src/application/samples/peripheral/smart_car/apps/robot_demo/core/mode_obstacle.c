#include "mode_obstacle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "motor_executor.h"
#include "robot_config.h"
#include "../robot_common.h"
#include "soc_osal.h"

// ================= 参数配置 =================
#define OBSTACLE_LIMIT 20.0f // 障碍物判定距离 (cm)
#define TIME_BACK_MS 300     // 后退一下的时间
#define TIME_TURN_90_MS 650  // 原地转90度所需时间
#define TIME_WAIT_STABLE 300 // 每次转完停顿检测的时间

#define OBST_FWD_SPEED 60
#define OBST_BACK_SPEED 60
#define OBST_TURN_SPEED 60

#define OBST_TICK_MS 20
#define OBST_TASK_STACK_SIZE 2048
#define OBST_TASK_PRIO 22
#define OBST_EVENT_STOP 0x01

// ================= 非阻塞状态机 =================
typedef enum {
    OBST_STATE_FORWARD = 0,
    OBST_STATE_STOP_BEFORE_BACK,
    OBST_STATE_BACKING,
    OBST_STATE_STOP_BEFORE_TURN,
    OBST_STATE_TURNING,
    OBST_STATE_STOP_BEFORE_CHECK,
    OBST_STATE_CHECKING,
} obstacle_state_t;

static obstacle_state_t g_obst_state = OBST_STATE_FORWARD;
static unsigned long long g_state_enter_tick = 0;

static osal_task *g_obst_task = NULL;
static osal_event g_obst_event;
static bool g_event_inited = false;
static volatile bool g_obst_running = false;

static void obst_push(int8_t l, int8_t r)
{
    motor_executor_push_cmd(l, r);
}

static void obst_transition(obstacle_state_t new_state)
{
    g_obst_state = new_state;
    g_state_enter_tick = osal_get_jiffies();
}

static bool obst_timeout(uint32_t ms)
{
    return (osal_get_jiffies() - g_state_enter_tick) >= osal_msecs_to_jiffies(ms);
}

static void obstacle_tick_once(void)
{
    float current_dist = hcsr04_get_distance();
    robot_mgr_update_distance(current_dist);

    switch (g_obst_state) {
        case OBST_STATE_FORWARD:
            if (current_dist > OBSTACLE_LIMIT) {
                obst_push(OBST_FWD_SPEED, OBST_FWD_SPEED);
            } else {
                printf("前方受阻(%.1fcm)，开始尝试寻找出口...\r\n", current_dist);
                obst_push(0, 0);
                obst_transition(OBST_STATE_STOP_BEFORE_BACK);
            }
            break;

        case OBST_STATE_STOP_BEFORE_BACK:
            if (obst_timeout(100)) {
                obst_push(-OBST_BACK_SPEED, -OBST_BACK_SPEED);
                obst_transition(OBST_STATE_BACKING);
            }
            break;

        case OBST_STATE_BACKING:
            if (obst_timeout(TIME_BACK_MS)) {
                obst_push(0, 0);
                obst_transition(OBST_STATE_STOP_BEFORE_TURN);
            }
            break;

        case OBST_STATE_STOP_BEFORE_TURN:
            if (obst_timeout(100)) {
                obst_push(-OBST_TURN_SPEED, OBST_TURN_SPEED);
                obst_transition(OBST_STATE_TURNING);
            }
            break;

        case OBST_STATE_TURNING:
            if (obst_timeout(TIME_TURN_90_MS)) {
                obst_push(0, 0);
                obst_transition(OBST_STATE_STOP_BEFORE_CHECK);
            }
            break;

        case OBST_STATE_STOP_BEFORE_CHECK:
            if (obst_timeout(TIME_WAIT_STABLE)) {
                obst_transition(OBST_STATE_CHECKING);
            }
            break;

        case OBST_STATE_CHECKING: {
            float new_dist = current_dist;
            printf("转向后距离: %.1f\r\n", new_dist);

            if (new_dist > OBSTACLE_LIMIT) {
                printf("找到出口！继续前进。\r\n");
                obst_push(OBST_FWD_SPEED, OBST_FWD_SPEED);
                obst_transition(OBST_STATE_FORWARD);
            } else {
                printf("仍受阻，继续转向...\r\n");
                obst_push(-OBST_TURN_SPEED, OBST_TURN_SPEED);
                obst_transition(OBST_STATE_TURNING);
            }
            break;
        }
    }
}

static int obstacle_task_entry(void *arg)
{
    (void)arg;
    printf("[Obstacle] 避障任务启动\r\n");

    while (g_obst_running) {
        int ret = osal_event_read(&g_obst_event, OBST_EVENT_STOP, OBST_TICK_MS, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        if (ret > 0 && ((unsigned int)ret & OBST_EVENT_STOP)) {
            break;
        }
        obstacle_tick_once();
    }

    motor_executor_push_cmd(0, 0);
    printf("[Obstacle] 避障任务退出\r\n");
    g_obst_task = NULL; // 退出前自清句柄，供 exit 同步
    return 0;
}

void mode_obstacle_enter(void)
{
    printf("进入智能避障模式\r\n");
    obst_transition(OBST_STATE_FORWARD);
    motor_executor_push_cmd(0, 0);

    if (!g_event_inited) {
        if (osal_event_init(&g_obst_event) == OSAL_SUCCESS) {
            g_event_inited = true;
        } else {
            printf("[Obstacle] 事件初始化失败\r\n");
            return;
        }
    }

    if (g_obst_task != NULL)
        return;

    g_obst_running = true;
    osal_kthread_lock();
    g_obst_task =
        osal_kthread_create((osal_kthread_handler)obstacle_task_entry, NULL, "obst_task", OBST_TASK_STACK_SIZE);
    if (g_obst_task != NULL) {
        osal_kthread_set_priority(g_obst_task, OBST_TASK_PRIO);
    }
    osal_kthread_unlock();
}

void mode_obstacle_exit(void)
{
    if (g_obst_task != NULL) {
        g_obst_running = false;
        if (g_event_inited) {
            osal_event_write(&g_obst_event, OBST_EVENT_STOP);
        }
        // 等待任务自行退出（最多 200ms），避免 enter 时跳过创建
        int wait = 0;
        while (g_obst_task != NULL && wait < 20) {
            osal_msleep(10);
            wait++;
        }
        g_obst_task = NULL; // 兜底
    }
    motor_executor_push_cmd(0, 0);
    obst_transition(OBST_STATE_FORWARD);
}
