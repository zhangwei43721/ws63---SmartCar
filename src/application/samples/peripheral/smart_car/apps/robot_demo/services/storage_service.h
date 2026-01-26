#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <stdint.h>
#include "errcode.h"

/**
 * @brief 初始化存储服务，加载或初始化 NV 配置
 */
void storage_service_init(void);

/**
 * @brief 获取 PID 参数
 * @param kp 输出 Kp 值
 * @param ki 输出 Ki 值
 * @param kd 输出 Kd 值
 * @param speed 输出基础速度
 */
void storage_service_get_pid_params(float *kp, float *ki, float *kd, int16_t *speed);

/**
 * @brief 保存 PID 参数到 NV
 * @param kp Kp 值
 * @param ki Ki 值
 * @param kd Kd 值
 * @param speed 基础速度
 * @return 错误码
 */
errcode_t storage_service_save_pid_params(float kp, float ki, float kd, int16_t speed);

/**
 * @brief 获取 WiFi 配置
 * @param ssid 输出 SSID 缓冲区（至少 32 字节）
 * @param password 输出密码缓冲区（至少 64 字节）
 */
void storage_service_get_wifi_config(char *ssid, char *password);

/**
 * @brief 保存 WiFi 配置到 NV
 * @param ssid SSID
 * @param password 密码
 * @return 错误码
 */
errcode_t storage_service_save_wifi_config(const char *ssid, const char *password);

#endif
