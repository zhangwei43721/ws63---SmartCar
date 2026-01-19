#ifndef MODE_COMMON_H
#define MODE_COMMON_H

#include "../robot_common.h"
#include "robot_config.h" // Ensure we have the config definitions
#include "soc_osal.h"
#include <stdbool.h>

/**
 * @brief 发送遥测数据
 * @param mode 模式名称
 * @param data JSON格式的额外数据
 */
void send_telemetry(const char *mode, const char *data);

#endif
