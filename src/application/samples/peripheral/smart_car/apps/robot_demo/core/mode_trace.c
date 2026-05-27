#include "mode_trace.h"

#include <stdio.h>

#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../../../platform/storage_service.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../robot_common.h"
#include "soc_osal.h"

#define TRACE_SPEED_FORWARD 40
#define TRACE_LOST_TIMEOUT_MS 300
#define TRACE_SEARCH_SPEED 30
#define TRACE_TICK_MS 20

#define TRACE_TASK_STACK_SIZE 2048
#define TRACE_TASK_PRIO 22
#define TRACE_EVENT_STOP 0x01

#define TRACE_DETECT_BLACK 0

#define TRACE_PID_MSG_DEPTH 8

// PID 参数 + 运行态打包：所有字段只能被 trace_task 写入（单写者）
typedef struct {
    float kp;
    float ki;
    float kd;
    int base_speed;
    // 运行态
    float last_error;
    float integral;
    float last_valid_error;
    unsigned long long last_seen_tick;
} pid_ctx_t;

// 设参命令（UDP 任务 -> trace_task）
typedef struct {
    uint8_t op;     // 1=set, 2=save
    uint8_t type;   // PID_PARAM_*
    int32_t value;
} pid_msg_t;

#define PID_OP_SET  1
#define PID_OP_SAVE 2

static pid_ctx_t g_pid = {
    .kp = 16.0f, .ki = 0.0f, .kd = 0.0f, .base_speed = TRACE_SPEED_FORWARD,
    .last_error = 0, .integral = 0, .last_valid_error = 0, .last_seen_tick = 0,
};

static osal_task *g_trace_task = NULL;
static osal_event g_trace_event;
static bool g_event_inited = false;

// 退出同步信号量
static osal_semaphore g_trace_exit_sem;
static bool g_exit_sem_inited = false;

// PID 设参消息队列：UDP 任务 -> trace_task，单写者消费
static unsigned long g_pid_msgq = 0;
static bool g_pid_msgq_inited = false;

typedef struct {
    uint8_t left;
    uint8_t middle;
    uint8_t right;
    float error;
} TraceErrorMap;

static const TraceErrorMap g_trace_error_table[] = {
    {1, 1, 0, -1.0f}, {0, 1, 1, 1.0f}, {1, 0, 0, -2.0f}, {0, 0, 1, 2.0f}, {0, 1, 0, 0.0f}, {1, 1, 1, 0.0f},
};

static float calculate_trace_error(unsigned int left, unsigned int middle, unsigned int right)
{
    int table_size = sizeof(g_trace_error_table) / sizeof(g_trace_error_table[0]);
    for (int i = 0; i < table_size; i++) {
        if (g_trace_error_table[i].left == left && g_trace_error_table[i].middle == middle &&
            g_trace_error_table[i].right == right) {
            return g_trace_error_table[i].error;
        }
    }
    return 0.0f;
}

static float calculate_pid(float error)
{
    g_pid.integral += error;
    if (g_pid.integral > 50)
        g_pid.integral = 50;
    if (g_pid.integral < -50)
        g_pid.integral = -50;
    if (error > 1 || error < -1)
        g_pid.integral = 0;

    float p_term = g_pid.kp * error;
    float i_term = g_pid.ki * g_pid.integral;
    float d_term = g_pid.kd * (error - g_pid.last_error);
    g_pid.last_error = error;
    return p_term + i_term + d_term;
}

// 在 trace_task 上下文内消费设参消息（单写者）
static void drain_pid_messages(void)
{
    if (!g_pid_msgq_inited)
        return;
    pid_msg_t msg;
    unsigned int sz = sizeof(msg);
    while (osal_msg_queue_read_copy(g_pid_msgq, &msg, &sz, OSAL_MSGQ_NO_WAIT) == OSAL_SUCCESS) {
        if (msg.op == PID_OP_SET) {
            switch (msg.type) {
                case 1: g_pid.kp = (float)msg.value / 1000.0f; break;
                case 2: g_pid.ki = (float)msg.value / 10000.0f; break;
                case 3: g_pid.kd = (float)msg.value / 500.0f; break;
                case 4: g_pid.base_speed = msg.value; break;
                default: break;
            }
            g_pid.integral = 0;
            g_pid.last_error = 0;
            printf("PID Set: Kp=%d/1000 Ki=%d/10000 Kd=%d/500 Speed=%d\r\n",
                   (int)(g_pid.kp * 1000), (int)(g_pid.ki * 10000), (int)(g_pid.kd * 500), g_pid.base_speed);
        } else if (msg.op == PID_OP_SAVE) {
            errcode_t ret = storage_service_save_pid_params(g_pid.kp, g_pid.ki, g_pid.kd, (int16_t)g_pid.base_speed);
            printf("PID Save: Kp=%d/1000 Ki=%d/10000 Kd=%d/500 Speed=%d 结果=%d\r\n",
                   (int)(g_pid.kp * 1000), (int)(g_pid.ki * 10000), (int)(g_pid.kd * 500), g_pid.base_speed, ret);
        }
        sz = sizeof(msg);
    }
}

static void trace_tick_once(void)
{
    tcrt5000_sample();
    drain_pid_messages();

    uint32_t adc_l, adc_m, adc_r;
    tcrt5000_snapshot(&adc_l, &adc_m, &adc_r);

    unsigned int left = (adc_l >= TCRT5000_LEFT_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
    unsigned int middle = (adc_m >= TCRT5000_MIDDLE_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
    unsigned int right = (adc_r >= TCRT5000_RIGHT_THRESHOLD) ? TCRT5000_ON_BLACK : TCRT5000_ON_WHITE;
    unsigned long long now = osal_get_jiffies();

    static int debug_cnt = 0;
    if (++debug_cnt >= 20) {
        debug_cnt = 0;
        printf("TRACE: L=%d M=%d R=%d, ADC: L=%u M=%u R=%u mV\n", left, middle, right,
               (unsigned)adc_l, (unsigned)adc_m, (unsigned)adc_r);
    }

    robot_mgr_update_ir_status(left, middle, right);
    float error = calculate_trace_error(left, middle, right);

    if (left == TRACE_DETECT_BLACK || middle == TRACE_DETECT_BLACK || right == TRACE_DETECT_BLACK) {
        g_pid.last_seen_tick = now;
        g_pid.last_valid_error = error;

        float pid_output = calculate_pid(error);
        int current_base_speed = g_pid.base_speed;
        if (error >= 2 || error <= -2)
            current_base_speed = (int)(g_pid.base_speed * 0.6f);
        else if (error >= 1 || error <= -1)
            current_base_speed = (int)(g_pid.base_speed * 0.9f);

        int pid_out_int = (int)(pid_output > 0 ? (pid_output + 0.5f) : (pid_output - 0.5f));

        int left_speed = current_base_speed + pid_out_int;
        int right_speed = current_base_speed - pid_out_int;
        if (left_speed > 100) left_speed = 100;
        if (left_speed < -100) left_speed = -100;
        if (right_speed > 100) right_speed = 100;
        if (right_speed < -100) right_speed = -100;

        bsp_motor_push_cmd((int8_t)left_speed, (int8_t)right_speed);
    } else {
        if (now - g_pid.last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
            int search_speed = TRACE_SEARCH_SPEED;
            if (g_pid.last_valid_error < -0.5f) {
                bsp_motor_push_cmd(search_speed, -search_speed / 2);
            } else if (g_pid.last_valid_error > 0.5f) {
                bsp_motor_push_cmd(-search_speed / 2, search_speed);
            } else {
                bsp_motor_push_cmd(search_speed, search_speed);
            }
        } else {
            bsp_motor_push_cmd(0, 0);
        }
    }
}

static int trace_task_entry(void *arg)
{
    (void)arg;
    printf("[Trace] 循迹任务启动\r\n");

    while (1) {
        int ret =
            osal_event_read(&g_trace_event, TRACE_EVENT_STOP, TRACE_TICK_MS, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        if (ret > 0 && ((unsigned int)ret & TRACE_EVENT_STOP)) {
            break;
        }
        trace_tick_once();
    }

    bsp_motor_push_cmd(0, 0);
    printf("[Trace] 循迹任务退出\r\n");
    if (g_exit_sem_inited) {
        osal_sem_up(&g_trace_exit_sem);
    }
    return 0;
}

// 从外部任务（UDP）调用：只 enqueue，不直接改 PID
void mode_trace_set_pid(int type, int value)
{
    if (!g_pid_msgq_inited)
        return;
    pid_msg_t msg = {.op = PID_OP_SET, .type = (uint8_t)type, .value = value};
    (void)osal_msgq_overwrite(g_pid_msgq, TRACE_PID_MSG_DEPTH, &msg, sizeof(msg));
}

void mode_trace_save_pid(void)
{
    if (!g_pid_msgq_inited)
        return;
    pid_msg_t msg = {.op = PID_OP_SAVE, .type = 0, .value = 0};
    (void)osal_msgq_overwrite(g_pid_msgq, TRACE_PID_MSG_DEPTH, &msg, sizeof(msg));
}

void mode_trace_enter(void)
{
    printf("进入循迹模式...\r\n");

    g_pid.last_seen_tick = osal_get_jiffies();
    g_pid.last_error = 0;
    g_pid.integral = 0;
    g_pid.last_valid_error = 0;

    float kp, ki, kd;
    int16_t speed;
    storage_service_get_pid_params(&kp, &ki, &kd, &speed);
    g_pid.kp = kp;
    g_pid.ki = ki;
    g_pid.kd = kd;
    g_pid.base_speed = speed;

    if (!g_pid_msgq_inited) {
        if (osal_msg_queue_create("trace_pid_q", TRACE_PID_MSG_DEPTH, &g_pid_msgq,
                                  0, sizeof(pid_msg_t)) == OSAL_SUCCESS) {
            g_pid_msgq_inited = true;
        }
    }

    if (!g_event_inited) {
        if (osal_event_init(&g_trace_event) == OSAL_SUCCESS) {
            g_event_inited = true;
        } else {
            printf("[Trace] 事件初始化失败\r\n");
            return;
        }
    }
    if (!g_exit_sem_inited) {
        osal_sem_binary_sem_init(&g_trace_exit_sem, 0);
        g_exit_sem_inited = true;
    }
    while (osal_sem_trydown(&g_trace_exit_sem) == OSAL_SUCCESS) { }

    if (g_trace_task != NULL)
        return;

    g_trace_task = robot_task_create_locked("trace_task", (osal_kthread_handler)trace_task_entry, NULL,
                                            TRACE_TASK_STACK_SIZE, TRACE_TASK_PRIO);
}

void mode_trace_exit(void)
{
    if (g_trace_task != NULL) {
        if (g_event_inited) {
            osal_event_write(&g_trace_event, TRACE_EVENT_STOP);
        }
        if (g_exit_sem_inited) {
            (void)osal_sem_down_timeout(&g_trace_exit_sem, 500);
        }
        g_trace_task = NULL;
    }
    bsp_motor_push_cmd(0, 0);
}
