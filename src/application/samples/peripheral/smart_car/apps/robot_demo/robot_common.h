#ifndef ROBOT_COMMON_H
#define ROBOT_COMMON_H

#include <stdint.h>

typedef enum {
    CAR_STOP_STATUS = 0,
    CAR_TRACE_STATUS,
    CAR_OBSTACLE_AVOIDANCE_STATUS,
    CAR_WIFI_CONTROL_STATUS,
    CAR_BT_CONTROL_STATUS
} CarStatus;

// 转向方向常量
#define CAR_TURN_LEFT 0
#define CAR_TURN_RIGHT 1

/**
 * @brief 机器人实时状态结构体
 * 用于向 Web 前端提供实时状态数据
 */
typedef struct {
    CarStatus mode;           // 当前模式 (0:Standby, 1:Trace, 2:Avoid, 3:Remote)
    unsigned int servo_angle; // 当前舵机角度 (0-180)
    float distance;           // 当前超声波距离 (cm)
    unsigned int ir_left;     // 左红外状态 (0:黑线, 1:白色)
    unsigned int ir_middle;   // 中红外状态 (0:黑线, 1:白色)
    unsigned int ir_right;    // 右红外状态 (0:黑线, 1:白色)
} RobotState;

#endif
