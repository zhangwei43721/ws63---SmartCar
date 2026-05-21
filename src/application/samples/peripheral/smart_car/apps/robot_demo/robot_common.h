#ifndef ROBOT_COMMON_H
#define ROBOT_COMMON_H

#include <stdint.h>

/**
 * @brief 小车运行模式枚举
 */
typedef enum {
  CAR_STOP_STATUS = 0,           /* 停止模式：小车停止运动 */
  CAR_TRACE_STATUS,              /* 循迹模式：根据红外传感器进行黑线跟踪 */
  CAR_OBSTACLE_AVOIDANCE_STATUS, /* 避障模式：根据超声波传感器自动避障 */
  CAR_WIFI_CONTROL_STATUS,       /* WiFi遥控模式：通过UDP/WiFi接收控制命令 */
} CarStatus;

/**
 * @brief WiFi 连接状态枚举
 */
typedef enum {
  WIFI_STATUS_DISCONNECTED = 0, /* 未连接 */
  WIFI_STATUS_CONNECTING,       /* 连接中 */
  WIFI_STATUS_CONNECTED,        /* 已连接 */
  WIFI_STATUS_AP_MODE           /* 热点模式（预留） */
} WifiConnectStatus;

/**
 * @brief 机器人实时状态结构体
 * 用于向 Web 前端提供实时状态数据
 */
typedef struct {
  CarStatus mode;          // 当前模式 (0:Standby, 1:Trace, 2:Avoid, 3:Remote)
  float distance;          // 当前超声波距离 (cm)
  unsigned int ir_left;    // 左红外状态 (0:黑线, 1:白色)
  unsigned int ir_middle;  // 中红外状态 (0:黑线, 1:白色)
  unsigned int ir_right;   // 右红外状态 (0:黑线, 1:白色)
} RobotState;

#endif
