#include "mode_obstacle.h"

#include "robot_mgr.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"

#include "soc_osal.h"

#include <stdio.h>

#include "../services/net_service.h"

static void engine_turn_left(void)
{
    sg90_set_angle(150);
    robot_mgr_update_servo_angle(150);
}

static void engine_turn_right(void)
{
    sg90_set_angle(30);
    robot_mgr_update_servo_angle(30);
}

static void regress_middle(void)
{
    sg90_set_angle(90);
    robot_mgr_update_servo_angle(90);
}

static unsigned int engine_go_where(void)
{
    float left_distance = 0.0f;
    float right_distance = 0.0f;

    engine_turn_left();
    osal_msleep(200);
    left_distance = hcsr04_get_distance();
    osal_msleep(100);

    regress_middle();
    osal_msleep(200);

    engine_turn_right();
    osal_msleep(200);
    right_distance = hcsr04_get_distance();
    osal_msleep(100);

    regress_middle();

    int left_cm_x100 = (int)(left_distance * 100.0f);
    int right_cm_x100 = (int)(right_distance * 100.0f);
    printf("左侧: %d.%02d cm, 右侧: %d.%02d cm\r\n", left_cm_x100 / 100, left_cm_x100 % 100, right_cm_x100 / 100,
            right_cm_x100 % 100);

    return (left_distance > right_distance) ? CAR_TURN_LEFT : CAR_TURN_RIGHT;
}

static void car_where_to_go(float distance)
{
    if (distance <= 0 || distance < DISTANCE_BETWEEN_CAR_AND_OBSTACLE) {
        car_stop();
        osal_msleep(200);

        car_backward();
        osal_msleep(400);
        car_stop();

        unsigned int ret = engine_go_where();
        if (ret == CAR_TURN_LEFT) {
            car_left();
            osal_msleep(400);
        } else {
            car_right();
            osal_msleep(400);
        }
        car_stop();
    } else {
        car_forward();
    }
}

void mode_obstacle_run(void)
{
    float distance = 0.0f;
    printf("进入避障模式...\r\n");
    regress_middle();

    unsigned long long last_report = 0;

    while (robot_mgr_get_status() == CAR_OBSTACLE_AVOIDANCE_STATUS) {
        distance = hcsr04_get_distance();
        robot_mgr_update_distance(distance);
        car_where_to_go(distance);

        unsigned long long now = osal_get_jiffies();
        if (now - last_report > osal_msecs_to_jiffies(500)) {
            last_report = now;
            int dist_x100 = (int)(distance * 100.0f);
            printf("[obstacle] dist=%d.%02dcm\r\n", dist_x100 / 100, dist_x100 % 100);

            char payload[96] = {0};
            (void)snprintf(payload, sizeof(payload), "{\"mode\":\"obstacle\",\"dist_x100\":%d}\n", dist_x100);
            (void)net_service_send_text(payload);
        }

        osal_msleep(50);
    }

    car_stop();
    regress_middle();
}

void mode_obstacle_tick(void)
{
}
