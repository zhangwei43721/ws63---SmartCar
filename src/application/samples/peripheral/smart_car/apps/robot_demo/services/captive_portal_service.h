/**
 * @file        captive_portal_service.h
 * @brief       AP 配网服务（Captive Portal）头文件
 * @details     在 AP 模式下启动 HTTP 服务器，提供 Web 页面供用户配置 WiFi
 */

#ifndef CAPTIVE_PORTAL_SERVICE_H
#define CAPTIVE_PORTAL_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief AP 服务模式枚举
 * @details 区分配网模式和控制模式，同一时刻只运行一种模式
 */
typedef enum {
    PORTAL_MODE_CONFIG = 0,   /* 配网模式：HTTP + DNS 劫持 */
    PORTAL_MODE_CONTROL,      /* 控制模式：仅 HTTP，关闭 DNS */
} portal_mode_t;

/**
 * @brief 初始化 AP 配网服务
 * @note 服务内部检测 WiFi 模式，仅在 AP 模式下启动 HTTP 服务器
 */
void captive_portal_service_init(void);

/**
 * @brief 检查配网服务是否正在运行
 * @return true 表示 HTTP 服务器正在运行
 */
bool captive_portal_service_is_running(void);

/**
 * @brief 获取当前 AP 配网 IP 地址字符串
 * @return IP 地址字符串，如 "192.168.1.1"
 */
const char* captive_portal_service_get_ap_ip(void);

/**
 * @brief 获取当前配网状态描述
 * @return 状态字符串，如 "等待配网" / "配网成功" / "配网失败"
 */
const char* captive_portal_service_get_status_text(void);

/**
 * @brief 设置 AP  portal 运行模式
 * @param mode PORTAL_MODE_CONFIG(配网) 或 PORTAL_MODE_CONTROL(控制)
 * @note 控制模式下会自动关闭 DNS 劫持，减少网络负载
 */
void captive_portal_set_mode(portal_mode_t mode);

/**
 * @brief 获取当前 AP portal 运行模式
 * @return 当前模式
 */
portal_mode_t captive_portal_get_mode(void);

#endif /* CAPTIVE_PORTAL_SERVICE_H */
