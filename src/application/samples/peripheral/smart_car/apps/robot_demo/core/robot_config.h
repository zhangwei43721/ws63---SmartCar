/**
 ****************************************************************************************************
 * @file        robot_config.h
 * @brief       智能小车配置常量定义
 * @note        集中管理所有硬件、时间、网络等配置参数
 ****************************************************************************************************
 */

#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* =============== 舵机配置 =============== */
#define SERVO_LEFT_ANGLE 180  // 舵机左转角度
#define SERVO_RIGHT_ANGLE 0   // 舵机右转角度
#define SERVO_MIDDLE_ANGLE 90 // 舵机中间角度

/* =============== 时间配置（毫秒） =============== */
#define SERVO_MOVE_DELAY_MS 350   // 舵机转动等待时间
#define SENSOR_STABILIZE_MS 50    // 传感器稳定等待时间
#define BACKWARD_MOVE_MS 400      // 后退时间
#define TURN_MOVE_MS 400          // 转向时间
#define REMOTE_CMD_TIMEOUT_MS 500 // 遥控命令超时时间

#define TELEMETRY_REPORT_MS 500     // 遥测数据上报间隔
#define MAIN_LOOP_DELAY_MS 20       // 主循环延时
#define MODE_CHECK_DELAY_MS 10      // 模式检查延时
#define STANDBY_UPDATE_DELAY_MS 500 // 待机模式更新延时

/* =============== 网络配置 =============== */
#define TCP_RECONNECT_DELAY_MS 2000 // TCP 重连延时
#define RECV_TIMEOUT_MS 100         // 接收超时时间
#define WIFI_RETRY_INTERVAL_MS 5000 // WiFi 重试间隔

/* =============== 缓冲区配置 =============== */
#define PAYLOAD_BUFFER_SIZE 128 // 数据负载缓冲区大小
#define IP_BUFFER_SIZE 32       // IP 地址缓冲区大小
#define JSON_BUFFER_SIZE 256    // JSON 数据缓冲区大小
#define MODE_BUFFER_SIZE 32     // 模式字符串缓冲区大小

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_CONFIG_H */
