#include "mode_trace.h"

/**
 * @brief 循迹模式主运行函数
 * @note 根据红外传感器状态控制小车前进、左转、右转或停止
 */
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

        // 更新红外传感器状态
        robot_mgr_update_ir_status(left, middle, right);

        if (middle == TCRT5000_ON_BLACK)
            car_forward();
        else if (left == TCRT5000_ON_BLACK)
            car_left();
        else if (right == TCRT5000_ON_BLACK)
            car_right();
        else
            car_stop();

        unsigned long long now = osal_get_jiffies();
        if (now - last_report > osal_msecs_to_jiffies(500)) {
            last_report = now;
            printf("[trace] L=%u M=%u R=%u\r\n", left, middle, right);

            char payload[96] = {0};
            (void)snprintf(payload, sizeof(payload), "{\"mode\":\"trace\",\"left\":%u,\"mid\":%u,\"right\":%u}\n", left,
                           middle, right);
            (void)net_service_send_text(payload);
        }

        osal_msleep(20);
    }

    car_stop();
}

/**
 * @brief 循迹模式周期回调函数（当前为空实现）
 */
void mode_trace_tick(void) {}
