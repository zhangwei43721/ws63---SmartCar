#include "mode_remote.h"

#include "robot_mgr.h"

#include "../services/net_service.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"

#include "soc_osal.h"

#include <stdio.h>

#define REMOTE_CMD_TIMEOUT_MS 500

static unsigned long long g_last_cmd_tick = 0;

static void apply_motor_cmd(uint8_t cmd)
{
    int8_t motor_cmd = (int8_t)cmd;

    if (motor_cmd > 5) {
        car_forward();
    } else if (motor_cmd < -5) {
        car_backward();
    } else {
        car_stop();
    }
}

static void apply_servo_cmd(uint8_t val)
{
    int8_t servo_cmd = (int8_t)val;
    int angle = 90 + (servo_cmd * 90) / 100;

    if (angle < (int)SG90_ANGLE_MIN) {
        angle = SG90_ANGLE_MIN;
    } else if (angle > (int)SG90_ANGLE_MAX) {
        angle = SG90_ANGLE_MAX;
    }

    sg90_set_angle((unsigned int)angle);
}

void mode_remote_run(void)
{
    printf("进入 WiFi 遥控模式...\r\n");

    car_stop();
    g_last_cmd_tick = osal_get_jiffies();

    while (robot_mgr_get_status() == CAR_WIFI_CONTROL_STATUS) {
        uint8_t cmd;
        uint8_t val;

        if (net_service_pop_cmd(&cmd, &val)) {
            apply_motor_cmd(cmd);
            apply_servo_cmd(val);
            g_last_cmd_tick = osal_get_jiffies();
        }

        unsigned long long now = osal_get_jiffies();
        if (now - g_last_cmd_tick > osal_msecs_to_jiffies(REMOTE_CMD_TIMEOUT_MS)) {
            car_stop();
        }

        osal_msleep(10);
    }

    car_stop();
}

void mode_remote_tick(void)
{
}
