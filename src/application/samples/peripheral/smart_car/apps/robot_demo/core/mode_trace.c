#include "mode_trace.h"

#include "robot_mgr.h"

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include "soc_osal.h"

#include <stdio.h>
#include "../services/net_service.h"

void mode_trace_run(void)
{
    unsigned int left = 0;
    unsigned int middle = 0;
    unsigned int right = 0;
    unsigned long long last_report = 0;

    printf("进入循迹模式...\r\n");

    while (robot_mgr_get_status() == CAR_TRACE_STATUS) {
        left = tcrt5000_get_left();
        middle = tcrt5000_get_middle();
        right = tcrt5000_get_right();

        if (middle == TCRT5000_ON_BLACK) {
            car_forward();
        } else if (left == TCRT5000_ON_BLACK) {
            car_left();
        } else if (right == TCRT5000_ON_BLACK) {
            car_right();
        } else {
            car_stop();
        }

        unsigned long long now = osal_get_jiffies();
        if (now - last_report > osal_msecs_to_jiffies(500)) {
            last_report = now;
            printf("[trace] L=%u M=%u R=%u\r\n", left, middle, right);

            char payload[96] = {0};
            (void)snprintf(payload, sizeof(payload), "{\"mode\":\"trace\",\"left\":%u,\"mid\":%u,\"right\":%u}\n",
                            left, middle, right);
            (void)net_service_send_text(payload);
        }

        osal_msleep(20);
    }

    car_stop();
}

void mode_trace_tick(void)
{
}
