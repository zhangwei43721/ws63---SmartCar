#include "mode_obstacle.h"


// 舵机角度定义
#define SERVO_LEFT 150
#define SERVO_RIGHT 30
#define SERVO_MIDDLE 90

// 延时定义 (ms)
#define DELAY_SERVO 350 // 舵机转动等待时间
#define DELAY_STABLE 50 // 传感器稳定等待
#define DELAY_BACK 400  // 后退时间
#define DELAY_TURN 400  // 转向时间

/**
 * @brief 设置舵机角度并等待到位，同时更新全局状态
 */
static void set_servo_angle_wait(int angle)
{
    sg90_set_angle(angle);
    robot_mgr_update_servo_angle(angle);
    osal_msleep(DELAY_SERVO);
}

/**
 * @brief 获取并更新距离信息
 */
static float get_distance_update(void)
{
    float dist = hcsr04_get_distance();
    robot_mgr_update_distance(dist);
    return dist;
}

/**
 * @brief 停车扫描判断方向
 * @return CAR_TURN_LEFT (左侧宽敞) 或 CAR_TURN_RIGHT (右侧宽敞)
 */
static unsigned int scan_and_decide_direction(void)
{
    // 1. 扫描左侧
    set_servo_angle_wait(SERVO_LEFT);
    osal_msleep(DELAY_STABLE); // 额外等待传感器读数稳定
    float left_dist = get_distance_update();

    // 2. 扫描右侧
    set_servo_angle_wait(SERVO_RIGHT);
    osal_msleep(DELAY_STABLE);
    float right_dist = get_distance_update();

    // 3. 舵机回中
    set_servo_angle_wait(SERVO_MIDDLE);

    // 打印调试信息 (保留原有的整数打印方式以兼容不支持%f的printf)
    int l_val = (int)(left_dist * 100);
    int r_val = (int)(right_dist * 100);
    printf("扫描结果: 左=%d.%02d cm, 右=%d.%02d cm\r\n", l_val / 100, l_val % 100, r_val / 100, r_val % 100);

    return (left_dist > right_dist) ? CAR_TURN_LEFT : CAR_TURN_RIGHT;
}

/**
 * @brief 根据距离执行避障动作
 */
static void perform_obstacle_avoidance(float distance)
{
    // 距离有效且小于阈值，或者读数异常(<=0)视为障碍物
    if (distance <= 0 || distance < DISTANCE_BETWEEN_CAR_AND_OBSTACLE) {
        // 1. 停车并后退
        car_stop();
        osal_msleep(200);
        car_backward();
        osal_msleep(DELAY_BACK);
        car_stop();

        // 2. 扫描环境决定转向
        unsigned int direction = scan_and_decide_direction();

        // 3. 执行转向
        if (direction == CAR_TURN_LEFT) {
            car_left();
        } else {
            car_right();
        }
        osal_msleep(DELAY_TURN);
        car_stop();

    } else {
        // 路径通畅，继续直行
        car_forward();
    }
}

void mode_obstacle_run(void)
{
    printf("进入避障模式...\r\n");
    set_servo_angle_wait(SERVO_MIDDLE); // 初始化舵机居中

    unsigned long long last_report_time = 0;
    char payload[96] = {0};

    while (robot_mgr_get_status() == CAR_OBSTACLE_AVOIDANCE_STATUS) {
        // 获取当前距离
        float distance = get_distance_update();

        // 执行避障逻辑
        perform_obstacle_avoidance(distance);

        // 每500ms上报一次遥测数据
        unsigned long long now = osal_get_jiffies();
        if (now - last_report_time > osal_msecs_to_jiffies(500)) {
            last_report_time = now;
            int dist_x100 = (int)(distance * 100.0f);

            printf("[obstacle] dist=%d.%02dcm\r\n", dist_x100 / 100, dist_x100 % 100);

            // 发送网络数据
            snprintf(payload, sizeof(payload), "{\"mode\":\"obstacle\",\"dist_x100\":%d}\n", dist_x100);
            net_service_send_text(payload);
        }

        // 循环间隔
        osal_msleep(50);
    }

    // 退出模式前复位
    car_stop();
    set_servo_angle_wait(SERVO_MIDDLE);
}

void mode_obstacle_tick(void)
{
    // 此模式下主要逻辑在 run 线程中，tick 可留空
}