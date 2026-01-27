#include "mode_remote.h"
#include "robot_config.h"
#include "robot_mgr.h"
#include "../services/udp_service.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"
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
    if (udp_service_pop_cmd(&motor1, &motor2, &servo)) {
        l9110s_set_differential(motor1, motor2);           // 应用电机控制
        sg90_set_angle((unsigned int)servo);               // 应用舵机控制
        robot_mgr_update_servo_angle((unsigned int)servo); // 更新状态
        g_last_cmd_tick = osal_get_jiffies();
    }

    // 命令超时自动停车
    unsigned long long now = osal_get_jiffies();
    if (now - g_last_cmd_tick > osal_msecs_to_jiffies(REMOTE_TIMEOUT)) {
        l9110s_set_differential(0, 0); // 双电机停止
        sg90_set_angle(SERVO_CENTER);
        robot_mgr_update_servo_angle(SERVO_CENTER);
    }
}

void mode_remote_exit(void)
{
    CAR_STOP();
}
