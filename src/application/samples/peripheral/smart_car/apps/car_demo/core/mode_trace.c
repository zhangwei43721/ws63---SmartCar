#include "mode_trace.h"
#include <stdio.h>
#include <math.h> // 提供 roundf
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../../../platform/storage_service.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "soc_osal.h"

#define TRACE_LOST_TIMEOUT_MS 300
#define TRACE_SEARCH_SPEED 30

// 传感器物理状态枚举（1代表黑线）
typedef enum {
    STATE_000 = 0,
    STATE_001 = 1,
    STATE_010 = 2,
    STATE_011 = 3,
    STATE_100 = 4,
    STATE_101 = 5,
    STATE_110 = 6,
    STATE_111 = 7
} trace_state_t;

// 状态映射表：枚举 -> [PID数学误差, 速度缩放比例]
typedef struct {
    float error;
    float speed_ratio;
} trace_action_t;

static const trace_action_t g_action_map[8] = {
    [STATE_000] = {0.0f, 1.0f},  // 丢线（外层单独处理）
    [STATE_001] = {2.0f, 0.6f},  // 严重偏左
    [STATE_010] = {0.0f, 1.0f},  // 完美居中
    [STATE_011] = {1.0f, 0.9f},  // 轻微偏左
    [STATE_100] = {-2.0f, 0.6f}, // 严重偏右
    [STATE_101] = {0.0f, 1.0f},  // 异常状态（当居中处理）
    [STATE_110] = {-1.0f, 0.9f}, // 轻微偏右
    [STATE_111] = {0.0f, 1.0f}   // 十字路口（直走）
};

// PID 参数与运行上下文
static struct {
    float kp, ki, kd;
    int base_speed;
    float last_error, integral, last_valid_error;
    unsigned long long last_seen_tick;
} g_pid = {.kp = 16.0f, .base_speed = 40};

// 统一管理 OS 资源
static struct {
    osal_task *task;
    osal_event event;
    osal_semaphore exit_sem;
    bool inited;
} g_os = {0};

// 根据偏差计算 PID 输出
static float calculate_pid(float error)
{
    g_pid.integral += error;

    // 积分限幅
    if (g_pid.integral > 50.0f)
        g_pid.integral = 50.0f;
    else if (g_pid.integral < -50.0f)
        g_pid.integral = -50.0f;

    // 偏离严重时主动清除积分，避免摆尾震荡
    if (fabs(error) > 1.0f)
        g_pid.integral = 0.0f;

    float p = g_pid.kp * error;
    float i = g_pid.ki * g_pid.integral;
    float d = g_pid.kd * (error - g_pid.last_error);
    g_pid.last_error = error;

    return p + i + d;
}

// 循迹轮询：单次 tick 逻辑
static void trace_tick_once(void)
{
    tcrt5000_sample();
    uint32_t adc_l, adc_m, adc_r;
    tcrt5000_snapshot(&adc_l, &adc_m, &adc_r);

    uint8_t l = (adc_l >= TCRT5000_LEFT_THRESHOLD);
    uint8_t m = (adc_m >= TCRT5000_MIDDLE_THRESHOLD);
    uint8_t r = (adc_r >= TCRT5000_RIGHT_THRESHOLD);

    car_mgr_update_ir_status(l ? TCRT5000_BLACK : TCRT5000_WHITE, 
                             m ? TCRT5000_BLACK : TCRT5000_WHITE,
                             r ? TCRT5000_BLACK : TCRT5000_WHITE);

    static int debug_cnt = 0;
    if (++debug_cnt >= 20) {
        debug_cnt = 0;
        printf("[循迹] 传感器: L=%d M=%d R=%d | ADC: %u %u %u mV\n", l, m, r, (unsigned)adc_l, (unsigned)adc_m, (unsigned)adc_r);
    }

    trace_state_t state = (trace_state_t)((l << 2) | (m << 1) | r);
    unsigned long long now = osal_get_jiffies();

    if (state != STATE_000) { // 在线上
        g_pid.last_seen_tick = now;
        g_pid.last_valid_error = g_action_map[state].error;

        float pid_out = calculate_pid(g_action_map[state].error);
        int base = (int)(g_pid.base_speed * g_action_map[state].speed_ratio);
        int pid_int = (int)roundf(pid_out);
        int left_target  = base + pid_int;
        int right_target = base - pid_int;

        if (left_target > 127)   left_target = 127;
        if (left_target < -128)  left_target = -128;
        if (right_target > 127)  right_target = 127;
        if (right_target < -128) right_target = -128;

        bsp_motor_push_cmd((int8_t)left_target, (int8_t)right_target);

    } else if (now - g_pid.last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) { // 短暂丢线寻线
        int speed = TRACE_SEARCH_SPEED;
        if (g_pid.last_valid_error < -0.5f) {
            // 偏右（左边踩线），需要向左急转弯 -> 右轮前进而左轮后退
            bsp_motor_push_cmd(-speed / 2, speed); 
        }
        else if (g_pid.last_valid_error > 0.5f) {
            // 偏左（右边踩线），需要向右急转弯 -> 左轮前进而右轮后退
            bsp_motor_push_cmd(speed, -speed / 2);
        }
        else {
            // 居中丢线，笔直前搜
            bsp_motor_push_cmd(speed, speed);
        }

    } else { // 彻底丢线停机
        bsp_motor_push_cmd(0, 0);
    }
}

// 循迹任务入口线程
static int trace_task_entry(void *arg)
{
    (void)arg;
    printf("[循迹] 循迹任务启动\r\n");

    // 只要不收到退出事件 (0x01)，就以 20ms 为周期死循环执行
    while (osal_event_read(&g_os.event, 0x01, 20, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR) <= 0) {
        trace_tick_once();
    }

    bsp_motor_push_cmd(0, 0);
    printf("[循迹] 循迹任务退出\r\n");
    osal_sem_up(&g_os.exit_sem);
    return 0;
}

// 外部接口：设置 PID 参数（直接赋值，剔除臃肿的消息队列）
void mode_trace_set_pid(int type, int value)
{
    switch (type) {
        case 1:
            g_pid.kp = value / 1000.0f;
            break;
        case 2:
            g_pid.ki = value / 10000.0f;
            break;
        case 3:
            g_pid.kd = value / 500.0f;
            break;
        case 4:
            g_pid.base_speed = value;
            break;
    }
    // 设参后直接清空运行态
    g_pid.integral = 0.0f;
    g_pid.last_error = 0.0f;
    printf("[循迹] 临时参数加载: Kp=%.3f, Ki=%.4f, Kd=%.3f, 速度=%d\r\n", g_pid.kp, g_pid.ki, g_pid.kd,
           g_pid.base_speed);
}

// 外部接口：进入循迹模式
void mode_trace_enter(void)
{
    if (g_os.task != NULL)
        return; // 已经在跑了

    printf("进入循迹模式...\r\n");

    // 重置状态
    g_pid.last_seen_tick = osal_get_jiffies();
    g_pid.last_error = 0.0f;
    g_pid.integral = 0.0f;
    g_pid.last_valid_error = 0.0f;

    // 懒加载 OS 资源（仅初始化一次）
    if (!g_os.inited) {
        osal_event_init(&g_os.event);
        osal_sem_binary_sem_init(&g_os.exit_sem, 0);
        g_os.inited = true;
    }

    // 读Flash参数并强转设入
    int16_t speed;
    storage_service_get_pid_params(&g_pid.kp, &g_pid.ki, &g_pid.kd, &speed);
    g_pid.base_speed = speed;

    (void)osal_sem_trydown(&g_os.exit_sem); // 抽空旧信号
    g_os.task = car_task_create_locked("trace_task", (osal_kthread_handler)trace_task_entry, NULL, 2048, 22);
}

// 外部接口：退出循迹模式
void mode_trace_exit(void)
{
    if (g_os.task != NULL) {
        osal_event_write(&g_os.event, 0x01);              // 发送退出事件
        (void)osal_sem_down_timeout(&g_os.exit_sem, 500); // 等待任务死透
        g_os.task = NULL;
    }

    // 参数比对固化逻辑
    float s_kp, s_ki, s_kd;
    int16_t s_speed;
    storage_service_get_pid_params(&s_kp, &s_ki, &s_kd, &s_speed);

    if (g_pid.kp != s_kp || g_pid.ki != s_ki || g_pid.kd != s_kd || g_pid.base_speed != s_speed) {
        errcode_t ret = storage_service_save_pid_params(g_pid.kp, g_pid.ki, g_pid.kd, (int16_t)g_pid.base_speed);
        printf("[循迹] 保存配置到NV: Kp=%.3f Ki=%.4f Kd=%.3f SPD=%d (Ret=%d)\r\n", g_pid.kp, g_pid.ki, g_pid.kd,
               g_pid.base_speed, ret);
    }

    bsp_motor_push_cmd(0, 0);
}