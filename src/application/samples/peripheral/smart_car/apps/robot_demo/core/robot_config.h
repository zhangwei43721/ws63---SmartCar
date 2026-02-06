/**
 ****************************************************************************************************
 * @file        robot_config.h
 * @brief       智能小车配置常量定义
 ****************************************************************************************************
 */

#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

/* 任务配置 */
#define TASK_STACK_SIZE (1024 * 10)  // 任务栈大小
#define TASK_PRIO 25                 // 任务优先级

/* 时间配置 */
#define SENSOR_DELAY 50     // 传感器稳定等待
#define BACKWARD_TIME 400   // 后退时间
#define TURN_TIME 400       // 转向时间
#define REMOTE_TIMEOUT 500  // 遥控命令超时
#define LOOP_DELAY 20       // 主循环延时
#define STANDBY_DELAY 500   // 待机更新间隔

/* 网络配置 */
#define RECV_TIMEOUT 100          // 接收超时
#define WIFI_RETRY_INTERVAL 5000  // WiFi 重试间隔

/* 缓冲区配置 */
#define BUF_PAYLOAD 128  // 数据负载缓冲区
#define BUF_IP 32        // IP 地址缓冲区
#define BUF_MODE 32      // 模式字符串缓冲区

// 通用互斥锁操作宏
#define MUTEX_LOCK(mutex, inited)                \
  do {                                           \
    if (inited) (void)osal_mutex_lock(&(mutex)); \
  } while (0)

#define MUTEX_UNLOCK(mutex, inited)          \
  do {                                       \
    if (inited) osal_mutex_unlock(&(mutex)); \
  } while (0)

#endif /* ROBOT_CONFIG_H */
