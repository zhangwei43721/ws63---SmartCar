#include "mode_trace.h"
#include <stdio.h>
#include <math.h> // 提供 roundf
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../../../platform/storage_service.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "soc_osal.h"
#include "../services/debug_log_service.h"
#include "../services/udp_service.h"

// 循迹判定阈值（动态更新，初始使用默认宏值）
static uint16_t g_trace_l_threshold = TCRT5000_LEFT_THRESHOLD;
static uint16_t g_trace_m_threshold = TCRT5000_MIDDLE_THRESHOLD;
static uint16_t g_trace_r_threshold = TCRT5000_RIGHT_THRESHOLD;

typedef enum {
    TRACE_SUBMODE_PID = 0,          // PID巡线
    TRACE_SUBMODE_HARDCODED = 1,    // 机械式寻线
    TRACE_SUBMODE_CALIBRATION = 2   // 传感器校准
} trace_submode_t;

static trace_submode_t g_trace_submode = TRACE_SUBMODE_PID;

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

// 统一红外探头读取逻辑。将红外状态采样、状态量比对、全局状态（CarState）及 ADC 上报更新逻辑强内聚化整合，消除 PID 与硬编码模式各自的重复采样。
static void trace_read_sensors(uint8_t *l, uint8_t *m, uint8_t *r, trace_state_t *state)
{
    tcrt5000_sample();
    uint32_t adc_l, adc_m, adc_r;
    tcrt5000_snapshot(&adc_l, &adc_m, &adc_r);

    *l = (adc_l >= g_trace_l_threshold);
    *m = (adc_m >= g_trace_m_threshold);
    *r = (adc_r >= g_trace_r_threshold);

    car_mgr_update_ir_status(*l ? TCRT5000_BLACK : TCRT5000_WHITE, 
                             *m ? TCRT5000_BLACK : TCRT5000_WHITE,
                             *r ? TCRT5000_BLACK : TCRT5000_WHITE);

    car_mgr_update_adc_values(adc_l, adc_m, adc_r);
    *state = (trace_state_t)((*l << 2) | (*m << 1) | *r);
}

// 统一的丢线超时判定与恢复助手。封装了“丢线瞬间保持最后有效状态、超时未找回再判定断连”的定时策略，消除两种巡线子模式中高度重复的定时减法与溢出边界逻辑。
static bool trace_check_lost(trace_state_t state, unsigned long long now, float *error_out)
{
    if (state != STATE_000) {
        g_pid.last_seen_tick = now;
        g_pid.last_valid_error = g_action_map[state].error;
        *error_out = g_action_map[state].error;
        return true;
    }
    if (now - g_pid.last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
        *error_out = g_pid.last_valid_error;
        return true;
    }
    return false;
}

static void trace_debug_log(const char *tag, uint8_t l, uint8_t m, uint8_t r, int8_t cmd_l, int8_t cmd_r)
{
    static int debug_cnt = 0;
    if (++debug_cnt >= 20) {
        debug_cnt = 0;
        uint32_t adc_l, adc_m, adc_r;
        tcrt5000_snapshot(&adc_l, &adc_m, &adc_r);
        car_log("[循迹-%s] 传感器: L=%d M=%d R=%d | ADC: %u %u %u mV | 速度: L=%d R=%d\r\n", 
                tag, l, m, r, (unsigned)adc_l, (unsigned)adc_m, (unsigned)adc_r, cmd_l, cmd_r);
    }
}

// 循迹轮询：单次 tick 逻辑
static void trace_tick_once(void)
{
    uint8_t l, m, r;
    trace_state_t state;
    trace_read_sensors(&l, &m, &r, &state);

    switch (g_trace_submode) {
        case TRACE_SUBMODE_CALIBRATION: {
            static int send_cnt = 0;
            if (++send_cnt >= 5) { // 100ms 周期
                send_cnt = 0;
                udp_service_send_trace_info();
            }
            break;
        }

        case TRACE_SUBMODE_PID: {
            float error_to_use = 0.0f;
            unsigned long long now = osal_get_jiffies();
            bool should_run = trace_check_lost(state, now, &error_to_use);
            int8_t cmd_l = 0, cmd_r = 0;
            
            if (should_run) {
                float pid_out = calculate_pid(error_to_use);
                int base = (state != STATE_000) ? (int)(g_pid.base_speed * g_action_map[state].speed_ratio) : g_pid.base_speed;
                int pid_int = (int)roundf(pid_out);
                int left_target  = base + pid_int;
                int right_target = base - pid_int;

                if (left_target > 127)   left_target = 127;
                if (left_target < -128)  left_target = -128;
                if (right_target > 127)  right_target = 127;
                if (right_target < -128) right_target = -128;

                cmd_l = (int8_t)left_target;
                cmd_r = (int8_t)right_target;
            }
            bsp_motor_push_cmd(cmd_l, cmd_r);
            trace_debug_log("PID", l, m, r, cmd_l, cmd_r);
            break;
        }

        case TRACE_SUBMODE_HARDCODED: {
            float error_to_use = 0.0f;
            unsigned long long now = osal_get_jiffies();
            bool should_run = trace_check_lost(state, now, &error_to_use);
            int8_t cmd_l = 0, cmd_r = 0;

            if (should_run) {
                if (state != STATE_000) {
                    (void)calculate_pid(error_to_use); // 虽然计算但不应用纠偏
                    int base = (int)(g_pid.base_speed * g_action_map[state].speed_ratio);
                    cmd_l = (int8_t)base;
                    cmd_r = (int8_t)base;
                } else { // 短暂丢线寻线
                    int speed = TRACE_SEARCH_SPEED;
                    if (error_to_use < -0.5f) {
                        // 偏右，需要向左急转弯 -> 右轮前进而左轮后退
                        cmd_l = (int8_t)(-speed / 2);
                        cmd_r = (int8_t)speed;
                    }
                    else if (error_to_use > 0.5f) {
                        // 偏左，需要向右急转弯 -> 左轮前进而右轮后退
                        cmd_l = (int8_t)speed;
                        cmd_r = (int8_t)(-speed / 2);
                    }
                    else { 
                         // 居中丢线，笔直前搜
                        cmd_l = (int8_t)speed;
                        cmd_r = (int8_t)speed;
                    }
                }
            }
            bsp_motor_push_cmd(cmd_l, cmd_r);
            trace_debug_log("硬编码", l, m, r, cmd_l, cmd_r);
            break;
        }

        default:
            break;
    }
}

// 循迹任务入口线程
static int trace_task_entry(void *arg)
{
    (void)arg;
    printf("[循迹] 循迹任务启动\r\n");

    while (1) {
        int ret = osal_event_read(&g_os.event, 0x01, 20, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        if (ret > 0 && ((unsigned int)ret & 0x01)) {
            break;
        }
        trace_tick_once();
    }

    bsp_motor_push_cmd(0, 0);
    printf("[循迹] 循迹任务退出\r\n");
    osal_sem_up(&g_os.exit_sem);
    return 0;
}

// 外部接口：设置 PID 参数
void mode_trace_set_pid(int type, int value)
{
    switch (type) {
        case 1:
            g_pid.kp = value / 100.0f;
            break;
        case 2:
            g_pid.ki = value / 100.0f;
            break;
        case 3:
            g_pid.kd = value / 100.0f;
            break;
        case 4:
            g_pid.base_speed = value;
            break;
    }
    // 设参后直接清空运行态
    g_pid.integral = 0.0f;
    g_pid.last_error = 0.0f;
    car_log("[循迹] 临时参数加载: Kp=%.3f, Ki=%.4f, Kd=%.3f, 速度=%d\r\n", g_pid.kp, g_pid.ki, g_pid.kd,
           g_pid.base_speed);
}

// 外部接口：进入循迹模式
void mode_trace_enter(void)
{
    if (g_os.task != NULL)
        return; // 已经在跑了

    car_log("进入循迹模式...\r\n");
    g_trace_submode = TRACE_SUBMODE_CALIBRATION; // 默认是传感器校准模式

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

    // 读取循迹传感器校准阈值并同步至 CarState 与本地变量
    storage_service_get_trace_thresholds(&g_trace_l_threshold, &g_trace_m_threshold, &g_trace_r_threshold);
    car_mgr_update_thresholds(g_trace_l_threshold, g_trace_m_threshold, g_trace_r_threshold);

    (void)osal_sem_trydown(&g_os.exit_sem); // 抽空旧信号
    g_os.task = car_task_create_locked("trace_task", (osal_kthread_handler)trace_task_entry, NULL, 4096, 22);
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
        car_log("[循迹] 保存配置到NV: Kp=%.3f Ki=%.4f Kd=%.3f SPD=%d (Ret=%d)\r\n", g_pid.kp, g_pid.ki, g_pid.kd,
               g_pid.base_speed, ret);
    }

    bsp_motor_push_cmd(0, 0);
}

// 获取当前活跃的校准阈值
void mode_trace_get_thresholds(uint16_t *l, uint16_t *m, uint16_t *r)
{
    if (l) *l = g_trace_l_threshold;
    if (m) *m = g_trace_m_threshold;
    if (r) *r = g_trace_r_threshold;
}

// 更新循迹阈值，并保存到 NV
void mode_trace_update_thresholds(uint16_t l, uint16_t m, uint16_t r)
{
    g_trace_l_threshold = l;
    g_trace_m_threshold = m;
    g_trace_r_threshold = r;

    // 同步到存储服务
    (void)storage_service_save_trace_thresholds(l, m, r);
    // 同步更新全局 CarState
    car_mgr_update_thresholds(l, m, r);
}

// 设置循迹子模式 (0: PID巡线, 1: 硬编码巡线, 2: 传感器校准)
void mode_trace_set_submode(uint8_t submode)
{
    if (submode <= TRACE_SUBMODE_CALIBRATION) {
        g_trace_submode = (trace_submode_t)submode;
        car_log("[循迹] 切换子模式: %s\r\n", 
                submode == TRACE_SUBMODE_PID ? "PID巡线" : 
                (submode == TRACE_SUBMODE_HARDCODED ? "硬编码巡线" : "传感器校准"));
    }
}

// 检查是否处于传感器校准状态
bool mode_trace_is_calibrating(void)
{
    return (g_trace_submode == TRACE_SUBMODE_CALIBRATION);
}