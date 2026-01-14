#include "robot_mgr.h"

#include "mode_trace.h"
#include "mode_obstacle.h"
#include "mode_remote.h"

#include "../services/net_service.h"
#include "../services/ui_service.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include "soc_osal.h"

#include <stdio.h>

static CarStatus g_status = CAR_STOP_STATUS;

static void robot_mgr_run_standby(void)
{
    car_stop();

    while (robot_mgr_get_status() == CAR_STOP_STATUS) {
        char ip_line[32] = {0};
        const char *ip = net_service_get_ip();

        if (ip != NULL) {
            (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip);
        } else {
            (void)snprintf(ip_line, sizeof(ip_line), "IP: Pending");
        }

        ui_render_standby(net_service_is_connected() ? "WiFi: Connected" : "WiFi: Connecting", ip_line);
        osal_msleep(500);
    }

    car_stop();
}

void robot_mgr_init(void)
{
    l9110s_init();
    hcsr04_init();
    tcrt5000_init();
    sg90_init();

    ui_service_init();
    net_service_init();

    robot_mgr_set_status(CAR_STOP_STATUS);

    printf("RobotMgr: initialized\r\n");
}

CarStatus robot_mgr_get_status(void)
{
    return g_status;
}

void robot_mgr_set_status(CarStatus status)
{
    g_status = status;
    ui_show_mode_page(status);
}

void robot_mgr_process_loop(void)
{
    CarStatus status = robot_mgr_get_status();

    switch (status) {
        case CAR_STOP_STATUS:
            robot_mgr_run_standby();
            break;
        case CAR_TRACE_STATUS:
            mode_trace_run();
            break;
        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            mode_obstacle_run();
            break;
        case CAR_WIFI_CONTROL_STATUS:
            mode_remote_run();
            break;
        default:
            break;
    }
}
