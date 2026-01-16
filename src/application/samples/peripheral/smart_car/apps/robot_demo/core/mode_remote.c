#include "mode_remote.h"

#define REMOTE_CMD_TIMEOUT_MS 500

static unsigned long long g_last_cmd_tick = 0;

/**
 * @brief 执行电机控制命令
 * @param cmd 电机命令值（正数前进，负数后退，0停止）
 */
static void apply_motor_cmd(int8_t cmd)
{
    if (cmd > 0)
        car_forward();
    else if (cmd < 0)
        car_backward();
    else
        car_stop();
}

/**
 * @brief 执行舵机控制命令
 * @param val 舵机命令值（-100到100，0为中心）
 */
static void apply_servo_cmd(int8_t val)
{
    if (val == 0) {
        sg90_set_angle(90);
        robot_mgr_update_servo_angle(90);
        return;
    }

    int sign = (val > 0) ? 1 : -1;
    int mag = (val > 0) ? val : -val;
    if (mag > 100)
        mag = 100;

    int offset = (mag * 90) / 100;
    int angle = 90 + (sign * offset);

    if (angle < (int)SG90_ANGLE_MIN)
        angle = SG90_ANGLE_MIN;
    else if (angle > (int)SG90_ANGLE_MAX)
        angle = SG90_ANGLE_MAX;

    sg90_set_angle((unsigned int)angle);
    robot_mgr_update_servo_angle((unsigned int)angle);
}

/**
 * @brief WiFi 遥控模式主运行函数
 * @note 从网络接收控制命令并执行电机和舵机控制
 */
void mode_remote_run(void)
{
    printf("进入 WiFi 遥控模式...\r\n");

    car_stop();
    g_last_cmd_tick = osal_get_jiffies();

    while (robot_mgr_get_status() == CAR_WIFI_CONTROL_STATUS) {
        int8_t motor;
        int8_t servo1;
        int8_t servo2;

        if (net_service_pop_cmd(&motor, &servo1, &servo2)) {
            apply_motor_cmd(motor);
            apply_servo_cmd(servo1);
            (void)servo2;
            g_last_cmd_tick = osal_get_jiffies();
        }

        unsigned long long now = osal_get_jiffies();
        if (now - g_last_cmd_tick > osal_msecs_to_jiffies(REMOTE_CMD_TIMEOUT_MS))
            car_stop();

        osal_msleep(10);
    }

    car_stop();
}

/**
 * @brief 遥控模式周期回调函数（空实现）
 */
void mode_remote_tick(void) {}
