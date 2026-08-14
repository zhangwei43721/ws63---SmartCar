#include "mode_obstacle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "car_state.h"
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

// 避障状态机：obstacle_step() 每 20ms 被调用，
// 定时阶段由 osal_timer 单次触发 0x02 推进
typedef enum {
    OBST_FORWARD = 0,       // 向前行驶，主动测距
    OBST_STOP_BEFORE_BACK,  // 刹车 100ms → 后退
    OBST_BACKING,           // 后退 300ms → 刹车
    OBST_STOP_BEFORE_TURN,  // 刹车 100ms → 转弯
    OBST_TURNING,           // 原地左转 650ms → 等待稳定
    OBST_STOP_BEFORE_CHECK, // 等 300ms 稳定 → 重新测距
    OBST_CHECKING,          // 读取 HC-SR04，通畅→FORWARD，受阻→TURNING
} obstacle_state_t;

static obstacle_state_t g_obst_state = OBST_FORWARD;

// 统一的 OS 资源生命周期管理器。将散落的 osal_task、osal_event、osal_timer
// 句柄以及各自的状态标志（inited）进行强内聚结构体化包装，方便统一初始化与销毁，消除野句柄风险。
static struct {
    osal_task *task;
    osal_event event;
    osal_semaphore exit_sem;
    osal_timer timer;
    bool inited;
} g_os = {0};

// 设置并推送电机速度命令（内部自主，无需看门狗喂狗，由模式 exit 显式停车）
static void obst_push(int8_t l, int8_t r)
{
    bsp_motor_push_cmd(l, r, MOTOR_SRC_AUTONOMOUS);
}

// 避障定时器回调：触发TIMER事件推进状态机
static void obst_timer_cb(unsigned long arg)
{
    (void)arg;
    if (g_os.inited)
        (void)osal_event_write(&g_os.event, 0x02);
}

// 重新配置并启动避障单次定时器
static void obst_arm_timer(uint32_t ms)
{
    if (!g_os.inited)
        return;
    osal_timer_mod(&g_os.timer, ms);
}

// 避障状态机单步推进：根据当前状态和定时器事件决定下一步动作
static void obstacle_step(bool timer_fired)
{
    switch (g_obst_state) {
        case OBST_FORWARD: { // 前进中，主动测距
            float d = hcsr04_get_distance();
            car_state_update_distance(d);
            if (d > OBSTACLE_LIMIT)
                obst_push(OBST_FWD_SPEED, OBST_FWD_SPEED);
            else {
                printf("前方受阻(%dcm)，开始尝试寻找出口...\r\n", (int)d);
                obst_push(0, 0);
                g_obst_state = OBST_STOP_BEFORE_BACK;
                obst_arm_timer(TIME_BRAKE_MS);
            }
            break;
        }
        case OBST_STOP_BEFORE_BACK: // 刹车停稳 → 准备后退
            if (timer_fired) {
                obst_push(-OBST_BACK_SPEED, -OBST_BACK_SPEED);
                g_obst_state = OBST_BACKING;
                obst_arm_timer(TIME_BACK_MS);
            }
            break;
        case OBST_BACKING: // 后退中，300ms 后停
            if (timer_fired) {
                obst_push(0, 0);
                g_obst_state = OBST_STOP_BEFORE_TURN;
                obst_arm_timer(TIME_BRAKE_MS);
            }
            break;
        case OBST_STOP_BEFORE_TURN: // 刹车停稳 → 准备转弯
            if (timer_fired) {
                obst_push(-OBST_TURN_SPEED, OBST_TURN_SPEED);
                g_obst_state = OBST_TURNING;
                obst_arm_timer(TIME_TURN_90_MS);
            }
            break;
        case OBST_TURNING: // 原地左转中，650ms 后停
            if (timer_fired) {
                obst_push(0, 0);
                g_obst_state = OBST_STOP_BEFORE_CHECK;
                obst_arm_timer(TIME_WAIT_STABLE);
            }
            break;
        case OBST_STOP_BEFORE_CHECK: // 停稳等传感器稳定 → 准备测距
            if (timer_fired) {
                g_obst_state = OBST_CHECKING;
                // 立刻进入 CHECKING 主动测距，无需再等 timer
            }
            break;
        case OBST_CHECKING: { // 测距判断：通畅→前进，受阻→继续转
            float d = hcsr04_get_distance();
            car_state_update_distance(d);
            printf("转向后距离: %dcm\r\n", (int)d);
            if (d > OBSTACLE_LIMIT) {
                printf("找到出口！继续前进。\r\n");
                obst_push(OBST_FWD_SPEED, OBST_FWD_SPEED);
                g_obst_state = OBST_FORWARD;
            } else {
                printf("仍受阻，继续转向...\r\n");
                obst_push(-OBST_TURN_SPEED, OBST_TURN_SPEED);
                g_obst_state = OBST_TURNING;
                obst_arm_timer(TIME_TURN_90_MS);
            }
            break;
        }
    }
}

// 避障任务入口：20ms周期轮询测距+喂看门狗，收到停止事件后退出
static int obstacle_task_entry(void *arg)
{
    (void)arg;
    printf("[Obstacle] 避障任务启动\r\n");

    while (1) {
        // 始终 20ms 唤醒：FORWARD/CHECKING 需要主动测距，其余状态需要喂 motor 400ms 看门狗
        // （TURNING=650ms > 400ms，不喂狗中途电机会被强停，看上去就是"抽搐"）
        int ret = osal_event_read(&g_os.event, 0x01 | 0x02, 20, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        if (ret > 0 && ((unsigned int)ret & 0x01))
            break;

        bool timer_fired = (ret > 0 && ((unsigned int)ret & 0x02));
        obstacle_step(timer_fired);
    }

    if (g_os.inited)
        osal_timer_stop(&g_os.timer);

    bsp_motor_push_cmd(0, 0, MOTOR_SRC_AUTONOMOUS);
    printf("[Obstacle] 避障任务退出\r\n");
    if (g_os.inited)
        osal_sem_up(&g_os.exit_sem);

    return 0;
}

// 进入避障模式：初始化状态机、事件、定时器、创建任务
void mode_obstacle_enter(void)
{
    printf("进入智能避障模式\r\n");
    g_obst_state = OBST_FORWARD;
    bsp_motor_push_cmd(0, 0, MOTOR_SRC_AUTONOMOUS);

    if (!g_os.inited) {
        if (osal_event_init(&g_os.event) == OSAL_SUCCESS) {
            osal_sem_binary_sem_init(&g_os.exit_sem, 0);
            g_os.timer.interval = TIME_BRAKE_MS;
            g_os.timer.handler = obst_timer_cb;
            g_os.timer.data = 0;
            if (osal_timer_init(&g_os.timer) == OSAL_SUCCESS) {
                g_os.inited = true;
            }
        }
        if (!g_os.inited) {
            printf("[Obstacle] OS 资源初始化失败\r\n");
            return;
        }
    }

    while (osal_sem_trydown(&g_os.exit_sem) == OSAL_SUCCESS) {
    }

    if (g_os.task != NULL)
        return;

    g_os.task = car_task_create_locked("obst_task", (osal_kthread_handler)obstacle_task_entry, NULL, 2048, 22);
}

// 退出避障模式：停止定时器、发送停止事件并等待任务退出。
// 返回 false 表示任务 500ms 内未退出（异常），句柄已保留，避免下次 enter 重复创建。
bool mode_obstacle_exit(void)
{
    if (g_os.task != NULL) {
        if (g_os.inited)
            osal_event_write(&g_os.event, 0x01);

        if (g_os.inited) {
            if (osal_sem_down_timeout(&g_os.exit_sem, 500) != OSAL_SUCCESS) {
                printf("[BUG] Obstacle 任务退出超时，保留句柄\r\n");
                bsp_motor_push_cmd(0, 0, MOTOR_SRC_AUTONOMOUS);
                g_obst_state = OBST_FORWARD;
                return false;
            }
        }

        g_os.task = NULL;
    }
    if (g_os.inited)
        osal_timer_stop(&g_os.timer);

    bsp_motor_push_cmd(0, 0, MOTOR_SRC_AUTONOMOUS);
    g_obst_state = OBST_FORWARD;
    return true;
}
