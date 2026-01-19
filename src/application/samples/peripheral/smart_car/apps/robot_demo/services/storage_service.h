#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <stdint.h>
#include "errcode.h"

// 机器人运行参数
typedef struct {
    uint16_t obstacle_threshold_cm; // 避障阈值（cm）
    uint16_t servo_center_angle;    // 舵机回中角度（0~180）
} robot_params_t;

/**
 * @brief 初始化存储服务，加载或初始化 NV 配置
 */
void storage_service_init(void);

/**
 * @brief 获取当前机器人参数
 * @param out_params 输出参数指针
 */
void storage_service_get_params(robot_params_t *out_params);

/**
 * @brief 保存机器人参数到 NV
 * @param params 参数指针
 * @return 错误码
 */
errcode_t storage_service_save_params(const robot_params_t *params);

/**
 * @brief 获取避障阈值便捷接口
 * @return 距离(cm)
 */
uint16_t storage_service_get_obstacle_threshold(void);

/**
 * @brief 获取舵机中位角度便捷接口
 * @return 角度(0-180)
 */
uint16_t storage_service_get_servo_center(void);

#endif
