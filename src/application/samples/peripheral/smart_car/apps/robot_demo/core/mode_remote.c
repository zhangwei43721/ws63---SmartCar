#include "mode_remote.h"
#include "robot_config.h"
#include "robot_mgr.h"
#include "../services/udp_service.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "soc_osal.h"
#include <stdio.h>

// 遥控命令超时时间
static unsigned long long g_last_cmd_tick = 0;

void mode_remote_enter(void)
{
    printf("进入 WiFi 遥控模式...\r\n");
    CAR_STOP();
    g_last_cmd_tick = osal_get_jiffies();
}

void mode_remote_tick(void)
{
    int8_t motor1, motor2, servo;

    // 如果有新命令，更新控制并重置超时计时器
    // 注意：虽然舵机功能已移除，但servo变量仍需保留以保持网络协议兼容性
    if (udp_service_pop_cmd(&motor1, &motor2, &servo)) {
        l9110s_set_differential(motor1, motor2);  // 应用电机控制
        // 舵机控制已移除，servo值被忽略
        g_last_cmd_tick = osal_get_jiffies();
    }

    // 命令超时自动停车
    unsigned long long now = osal_get_jiffies(); // 获取当前时间
    if (now - g_last_cmd_tick > osal_msecs_to_jiffies(REMOTE_TIMEOUT)) {
        l9110s_set_differential(0, 0); // 双电机停止
        // 舵机回中功能已移除
    }
}

void mode_remote_exit(void)
{
    CAR_STOP();
}
