#include "mode_trace.h"
#include "robot_config.h"
#include "robot_mgr.h"
#include "../services/storage_service.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include <stdio.h>

// 循迹速度参数配置
#define TRACE_SPEED_FORWARD 40    // 默认直行速度
#define TRACE_LOST_TIMEOUT_MS 300 // 丢失黑线后继续行驶的超时时间(ms)
#define TRACE_SEARCH_SPEED 30     // 丢线后搜索速度

// PID 参数
static float g_kp = 16.0f;                     /* 比例系数 Kp */
static float g_ki = 0.0f;                      /* 积分系数 Ki */
static float g_kd = 0.0f;                      /* 微分系数 Kd */
static int g_base_speed = TRACE_SPEED_FORWARD; /* 基础前进速度 */

static float g_last_error = 0; /* 上次误差值（用于微分计算） */
static float g_integral = 0;   /* 误差积分值（用于积分计算） */

static unsigned long long g_last_seen_tick = 0; /* 上次检测到黑线的时间 */
static float g_last_valid_error = 0;            /* 上次有效误差值（用于丢线后反向搜索） */

// 循迹传感器误差映射表
typedef struct {
    uint8_t left;
    uint8_t middle;
    uint8_t right;
    float error;
} TraceErrorMap;

// 误差查找表
// 左偏为负，右偏为正
static const TraceErrorMap g_trace_error_table[] = {
    // 左传感器 中传感器 右传感器 误差值
    {1, 1, 0, -1.0f}, // 左+中: 轻微左偏
    {0, 1, 1, 1.0f},  // 右+中: 轻微右偏
    {1, 0, 0, -2.0f}, // 仅左: 严重左偏
    {0, 0, 1, 2.0f},  // 仅右: 严重右偏
    {0, 1, 0, 0.0f},  // 仅中: 居中
    {1, 1, 1, 0.0f},  // 全检测: 居中
};

/**
 * @brief 根据传感器状态计算误差值
 * @param left 左传感器状态
 * @param middle 中传感器状态
 * @param right 右传感器状态
 * @return 误差值 (0表示居中，负值左偏，正值右偏)
 */
static float calculate_trace_error(unsigned int left, unsigned int middle, unsigned int right)
{
    int table_size = sizeof(g_trace_error_table) / sizeof(g_trace_error_table[0]);
    for (int i = 0; i < table_size; i++) {
        if (g_trace_error_table[i].left == left && g_trace_error_table[i].middle == middle &&
            g_trace_error_table[i].right == right) {
            return g_trace_error_table[i].error;
        }
    }
    return 0.0f; // 默认无误差（或未检测到）
}

void mode_trace_enter(void)
{
    printf("进入循迹模式...\r\n");
    // 进入模式时初始化时间戳，防止误判
    g_last_seen_tick = osal_get_jiffies();

    // 重置 PID 状态
    g_last_error = 0;
    g_integral = 0;
    g_last_valid_error = 0; // 重置上次有效误差

    // 从 NV 加载 PID 参数
    float kp, ki, kd;
    int16_t speed;
    storage_service_get_pid_params(&kp, &ki, &kd, &speed);
    g_kp = kp;
    g_ki = ki;
    g_kd = kd;
    g_base_speed = speed;
    printf("PID 从 NV 加载: Kp=%.2f Ki=%.3f Kd=%.2f Speed=%d\r\n", g_kp, g_ki, g_kd, g_base_speed);
}

// 设置 PID 参数
// type: 1=Kp, 2=Ki, 3=Kd, 4=Speed
void mode_trace_set_pid(int type, int value)
{
    if (type == 1)
        g_kp = (float)value / 1000.0f;
    else if (type == 2)
        g_ki = (float)value / 10000.0f;
    else if (type == 3)
        g_kd = (float)value / 500.0f;
    else if (type == 4)
        g_base_speed = value;
    printf("PID Set: Kp=%.2f Ki=%.3f Kd=%.2f Speed=%d\r\n", g_kp, g_ki, g_kd, g_base_speed);

    // 保存到 NV
    storage_service_save_pid_params(g_kp, g_ki, g_kd, (int16_t)g_base_speed);

    // 重置积分，避免突变
    g_integral = 0;
    g_last_error = 0;
}

/*
 * 循迹模式主循环
 */
// 根据用户指示：黑色是高电平
#define TRACE_DETECT_BLACK 1

/**
 * @brief PID 核心计算算法
 * @param error 当前误差
 * @return PID 输出值
 */
static float calculate_pid(float error)
{
    // 1. 累加积分
    g_integral += error;

    // 2. 积分限幅 (防止积分饱和)
    if (g_integral > 50)
        g_integral = 50;
    if (g_integral < -50)
        g_integral = -50;

    // 3. 积分分离 (大误差时不积分)
    if (error > 1 || error < -1) {
        g_integral = 0;
    }

    // 4. 计算 P, I, D 三项
    float p_term = g_kp * error;
    float i_term = g_ki * g_integral;
    float d_term = g_kd * (error - g_last_error);

    // 5. 更新上次误差
    g_last_error = error;

    return p_term + i_term + d_term;
}

void mode_trace_tick(void)
{
    unsigned int left = tcrt5000_get_left();
    unsigned int middle = tcrt5000_get_middle();
    unsigned int right = tcrt5000_get_right();
    unsigned long long now = osal_get_jiffies();

    // 更新红外传感器状态到全局状态
    robot_mgr_update_ir_status(left, middle, right);

    // 计算误差 Error
    float error = calculate_trace_error(left, middle, right);

    if (left == TRACE_DETECT_BLACK || middle == TRACE_DETECT_BLACK || right == TRACE_DETECT_BLACK) {
        g_last_seen_tick = now;
        g_last_valid_error = error; // 保存当前有效误差，用于丢线后反向搜索

        // PID 计算
        float pid_output = calculate_pid(error);

        // --- 优化策略 ---

        // 1. 动态速度调整 (Dynamic Speed)
        // 当误差较大(拐弯)时，降低基础速度，给车更多时间纠正，防止冲出跑道
        // 注意：速度不能过低，否则电机可能带不动
        int current_base_speed = g_base_speed;
        if (error >= 2 || error <= -2)
            current_base_speed = (int)(g_base_speed * 0.6f); // 降速至 60%
        else if (error >= 1 || error <= -1)
            current_base_speed = (int)(g_base_speed * 0.9f); // 90% (轻微减速)

        // 使用四舍五入而不是截断，以保留 0.5 级别的微调效果
        int pid_out_int = (int)(pid_output > 0 ? (pid_output + 0.5f) : (pid_output - 0.5f));

        // 如果 Kp 很小(如 0.5) 且误差很小(1)，pid_output=0.5 -> round -> 1.
        // 这样 Kp=0.5 就有了效果 (diff=1).
        // 之前的 (int)0.5 = 0, 所以无效.

        int left_speed = current_base_speed + pid_out_int;
        int right_speed = current_base_speed - pid_out_int;

        // 速度限幅 (-100 ~ 100)
        if (left_speed > 100)
            left_speed = 100;
        if (left_speed < -100)
            left_speed = -100;
        if (right_speed > 100)
            right_speed = 100;
        if (right_speed < -100)
            right_speed = -100;

        l9110s_set_differential((int8_t)left_speed, (int8_t)right_speed);

    } else {
        // 未检测到黑线
        if (now - g_last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
            // 刚丢失信号不久，使用反向搜索策略
            // 如果上次是左偏（误差为负），向右转来找线；如果上次是右偏（误差为正），向左转来找线
            int search_speed = TRACE_SEARCH_SPEED;

            if (g_last_valid_error < -0.5f) {
                // 上次左偏，现在向右转（左轮快，右轮慢）
                l9110s_set_differential(search_speed, -search_speed / 2);
            } else if (g_last_valid_error > 0.5f) {
                // 上次右偏，现在向左转（左轮慢，右轮快）
                l9110s_set_differential(-search_speed / 2, search_speed);
            } else {
                // 上次居中，继续直行搜索
                l9110s_set_differential(search_speed, search_speed);
            }
        } else {
            l9110s_set_differential(0, 0); // 超时仍未找到，停车
        }
    }
}

void mode_trace_exit(void)
{
    CAR_STOP();
}
