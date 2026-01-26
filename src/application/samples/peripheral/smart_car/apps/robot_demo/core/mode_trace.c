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

// PID 参数
static float g_kp = 16.0f;                     /* 比例系数 Kp */
static float g_ki = 0.0f;                      /* 积分系数 Ki */
static float g_kd = 0.0f;                      /* 微分系数 Kd */
static int g_base_speed = TRACE_SPEED_FORWARD; /* 基础前进速度 */

static float g_last_error = 0; /* 上次误差值（用于微分计算） */
static float g_integral = 0;   /* 误差积分值（用于积分计算） */

static unsigned long long g_last_seen_tick = 0; /* 上次检测到黑线的时间 */

void mode_trace_enter(void)
{
    printf("进入循迹模式...\r\n");
    // 进入模式时初始化时间戳，防止误判
    g_last_seen_tick = osal_get_jiffies();

    // 重置 PID 状态
    g_last_error = 0;
    g_integral = 0;

    // 从 NV 加载 PID 参数
    float kp, ki, kd;
    int16_t speed;
    storage_service_get_pid_params(&kp, &ki, &kd, &speed);
    g_kp = kp;
    g_ki = ki;
    g_kd = kd;
    g_base_speed = speed;
    printf("PID 从 NV 加载: Kp=%.2f Ki=%.3f Kd=%.2f Speed=%d\r\n",
           g_kp, g_ki, g_kd, g_base_speed);
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

void mode_trace_tick(void)
{
    unsigned int left = tcrt5000_get_left();
    unsigned int middle = tcrt5000_get_middle();
    unsigned int right = tcrt5000_get_right();
    unsigned long long now = osal_get_jiffies();

    // 更新红外传感器状态到全局状态
    robot_mgr_update_ir_status(left, middle, right);

    // 计算误差 Error
    // 使用查找表：左偏为负，右偏为正
    // 误差值越大，表示偏离越严重，需要更强的转向
    typedef struct {
        uint8_t left;
        uint8_t middle;
        uint8_t right;
        float error;
    } error_map_t;

    static const error_map_t error_table[] = {
        // 左传感器 中传感器 右传感器 误差值
        {1, 1, 0, -1.0f},  // 左+中: 轻微左偏
        {0, 1, 1,  1.0f},  // 右+中: 轻微右偏
        {1, 0, 0, -2.0f},  // 仅左: 严重左偏
        {0, 0, 1,  2.0f},  // 仅右: 严重右偏
        {0, 1, 0,  0.0f},  // 仅中: 居中
        {1, 1, 1,  0.0f},  // 全检测: 居中
    };

    // 查找对应的误差值
    float error = 0.0f;
    for (int i = 0; i < 6; i++) {
        if (error_table[i].left == left &&
            error_table[i].middle == middle &&
            error_table[i].right == right) {
            error = error_table[i].error;
            break;
        }
    }

    if (left == TRACE_DETECT_BLACK || middle == TRACE_DETECT_BLACK || right == TRACE_DETECT_BLACK) {
        g_last_seen_tick = now;

        // PID 计算
        g_integral += error;
        // 积分限幅 (优化：减小限幅范围，防止过大)
        if (g_integral > 50)
            g_integral = 50;
        if (g_integral < -50)
            g_integral = -50;

        // 积分分离：当误差较大时，不进行积分，防止过冲
        if (error > 1 || error < -1) {
            g_integral = 0;
        }

        float p_term = g_kp * error;
        float i_term = g_ki * g_integral;
        float d_term = g_kd * (error - g_last_error);

        float pid_output = p_term + i_term + d_term;
        g_last_error = error;

        // --- 优化策略 ---

        // 1. 动态速度调整 (Dynamic Speed)
        // 当误差较大(拐弯)时，降低基础速度，给车更多时间纠正，防止冲出跑道
        // 注意：速度不能过低，否则电机可能带不动
        int current_base_speed = g_base_speed;
        if (error >= 2 || error <= -2)
            current_base_speed = (int)(g_base_speed * 0.6f); // 降速至 60%
        else if (error >= 1 || error <= -1)
            current_base_speed = (int)(g_base_speed * 0.9f); // 90% (轻微减速)

        // 确保最小速度，防止电机停转 (根据经验 l9110s 至少需要 20-30 PWM)
        if (current_base_speed < 20)
            current_base_speed = 20;

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
        // 记忆功能：如果刚丢失信号不久，继续直行一段距离
        if (now - g_last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
            // 丢失信号时，保持上一次的转向相反的趋势可能比直行更好？
            // 暂时保持直行，或者可以尝试用 g_last_error 来决定转向
            // 这里简单处理为直行，避免过度转向
            l9110s_set_differential(g_base_speed, g_base_speed);
        } else {
            // 超时仍未找到，停车
            l9110s_set_differential(0, 0);
        }
    }
}

void mode_trace_exit(void)
{
    car_stop();
}
