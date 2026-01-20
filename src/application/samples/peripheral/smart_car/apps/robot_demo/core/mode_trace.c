#include "mode_trace.h"
#include "mode_common.h"
#include "robot_config.h"
#include "robot_mgr.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include <stdio.h>

// 循迹速度参数配置
#define TRACE_SPEED_FORWARD     40  // 默认直行速度
#define TRACE_LOST_TIMEOUT_MS   300 // 丢失黑线后继续行驶的超时时间(ms)

// PID 参数
static float g_kp = 25.0f; // 初始Kp (需要根据电机响应调整)
static float g_ki = 0.0f;
static float g_kd = 10.0f;
static int g_base_speed = TRACE_SPEED_FORWARD;

static float g_last_error = 0;
static float g_integral = 0;

static unsigned long long g_last_telemetry_time = 0;
static unsigned long long g_last_seen_tick = 0;

void mode_trace_enter(void)
{
    printf("进入循迹模式...\r\n");
    g_last_telemetry_time = 0;
    // 进入模式时初始化时间戳，防止误判
    g_last_seen_tick = osal_get_jiffies();
    
    // 重置 PID 状态
    g_last_error = 0;
    g_integral = 0;
}

// 设置 PID 参数
// type: 1=Kp, 2=Ki, 3=Kd, 4=Speed
void mode_trace_set_pid(int type, int value)
{
    // 前端发来的是 x100 后的值 (例如前端2 -> 后端200 -> 这里200)
    // 除以 100.0f 还原回 2.0
    // 如果用户觉得 2.0 还是太大，我们需要提高精度，或者改变除数
    // 但为了保持兼容，建议用户在前端设置更小的值？前端slider最小是0，step是1。
    // 如果用户设为1，kp=0.01。
    // 之前用户说 kp=2 才稳定。说明 kp=2.0 左右是合适的。
    // 但用户说 kp=2 "比较抖"，"太极限了"。
    // 可能意味着 kp=2 时震荡，kp<2 (即kp=1, val=100 -> kp=1.0) 也许反应不够快？
    // 或者用户想要 1.5？
    
    // 这里我们把除数改为 100.0f (保持不变)，但在前端或后端做手脚？
    // 不，直接修改这里的解析逻辑更直接。
    // 如果我们想支持更精细的调节，可以约定 value 是 x1000 ? 不行，后端写死了 x100。
    
    // 让我们保持这里的逻辑，去优化积分项。
    
    if (type == 1) g_kp = (float)value / 100.0f;
    else if (type == 2) g_ki = (float)value / 100.0f;
    else if (type == 3) g_kd = (float)value / 100.0f;
    else if (type == 4) g_base_speed = value;
    
    printf("PID Set: Kp=%.2f Ki=%.2f Kd=%.2f Speed=%d\r\n", g_kp, g_ki, g_kd, g_base_speed);
    
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
    // 假设：左偏为负，右偏为正
    // 传感器状态组合 -> 误差值
    // 0 1 0 (中) -> Error = 0
    // 1 0 0 (左) -> Error = -2 (大幅度左偏，需要右转) -- 等等，如果传感器在车头，左传感器压线说明车头偏右？
    // 通常定义：Error = 目标位置 - 当前位置。或者 Error = 线路位置 - 车中心位置。
    // 如果左传感器(L)压线，说明线在车的左边，车偏右了，需要向左转。
    // 如果右传感器(R)压线，说明线在车的右边，车偏左了，需要向右转。
    
    // 重新定义 Error: Error > 0 表示需要右转 (线在右边)，Error < 0 表示需要左转 (线在左边)
    // R压线 -> 线在右 -> Error = 1
    // L压线 -> 线在左 -> Error = -1
    
    float error = 0;
    int detected = 0;

    if (middle == TRACE_DETECT_BLACK) {
        error = 0;
        detected = 1;
    } 
    
    if (left == TRACE_DETECT_BLACK) {
        error -= 1.0f; // 左边有线，叠加负误差
        detected = 1;
    }
    
    if (right == TRACE_DETECT_BLACK) {
        error += 1.0f; // 右边有线，叠加正误差
        detected = 1;
    }
    
    // 特殊情况修正：如果只有左边压线，误差应该更大，比如 -2
    // 如果只有右边压线，误差 2
    // 如果左+中，误差 -1
    // 如果右+中，误差 1
    // 上面的叠加逻辑其实已经涵盖了部分：
    // L=1, M=1, R=0 -> error = -1 + 0 = -1
    // L=0, M=1, R=1 -> error = 0 + 1 = 1
    // L=1, M=0, R=0 -> error = -1. 但此时偏离更大，应该比L+M更激进。
    
    // 优化误差计算
    if (middle == TRACE_DETECT_BLACK && left == !TRACE_DETECT_BLACK && right == !TRACE_DETECT_BLACK) {
        error = 0;
    } else if (left == TRACE_DETECT_BLACK && middle == TRACE_DETECT_BLACK) {
        error = -1;
    } else if (right == TRACE_DETECT_BLACK && middle == TRACE_DETECT_BLACK) {
        error = 1;
    } else if (left == TRACE_DETECT_BLACK) {
        error = -2;
    } else if (right == TRACE_DETECT_BLACK) {
        error = 2;
    }

    if (detected) {
        g_last_seen_tick = now;
        
        // PID 计算
        g_integral += error;
        // 积分限幅 (优化：减小限幅范围，防止过大)
        if (g_integral > 50) g_integral = 50;
        if (g_integral < -50) g_integral = -50;
        
        // 积分分离：当误差较大时，不进行积分，防止过冲
        if (error > 1 || error < -1) {
             g_integral = 0;
        }

        float p_term = g_kp * error;
        float i_term = g_ki * g_integral;
        float d_term = g_kd * (error - g_last_error);
        
        float pid_output = p_term + i_term + d_term;
        g_last_error = error;
        
        // 应用 PID 到电机速度 (差速控制)
        // pid_output > 0 -> Error > 0 -> 需要右转 -> 左轮快，右轮慢
        // 左轮 = Base + PID, 右轮 = Base - PID
        
        int left_speed = g_base_speed + (int)pid_output;
        int right_speed = g_base_speed - (int)pid_output;
        
        // 速度限幅 (-100 ~ 100)
        if (left_speed > 100) left_speed = 100;
        if (left_speed < -100) left_speed = -100;
        if (right_speed > 100) right_speed = 100;
        if (right_speed < -100) right_speed = -100;
        
        l9110s_set_differential((int8_t)left_speed, (int8_t)right_speed);
        
    } else {
        // 未检测到黑线
        // 记忆功能：如果刚丢失信号不久，继续直行一段距离
        if (now - g_last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
            // 丢失信号时，保持上一次的转向趋势可能比直行更好？
            // 暂时保持直行，或者可以尝试用 g_last_error 来决定转向
            // 这里简单处理为直行，避免过度转向
            l9110s_set_differential(g_base_speed, g_base_speed);
        } else {
            // 超时仍未找到，停车
            l9110s_set_differential(0, 0);
        }
    }

    // 定期上报遥测数据 (包含 PID 调试信息)
    if (now - g_last_telemetry_time > osal_msecs_to_jiffies(TELEMETRY_REPORT_MS)) {
        char data[128];
        snprintf(data, sizeof(data), "\"left\":%u,\"mid\":%u,\"right\":%u,\"err\":%d,\"kp\":%d,\"kd\":%d", 
                 left, middle, right, (int)(error*10), (int)(g_kp*100), (int)(g_kd*100));
        send_telemetry("trace", data);
        g_last_telemetry_time = now;
    }
}

void mode_trace_exit(void)
{
    car_stop();
}
