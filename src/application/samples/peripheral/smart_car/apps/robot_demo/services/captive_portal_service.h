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
 * @brief 初始化 AP 配网服务
 * @note 服务内部检测 WiFi 模式，仅在 AP 模式下启动 HTTP 服务器
 */
void captive_portal_service_init(void);

/**
 * @brief 获取当前 AP 配网 IP 地址字符串
 * @return IP 地址字符串，如 "192.168.1.1"
 */
const char *captive_portal_service_get_ap_ip(void);

// wifi_mgr 在 AP_READY / AP_STOPPED / STA_FAIL 时通知 portal
void captive_portal_service_notify_ap_ready(void);   /* 通知 portal：AP 热点已就绪 */
void captive_portal_service_notify_ap_stopped(void); /* 通知 portal：AP 热点已停止 */
void captive_portal_service_notify_sta_fail(void);   /* 通知 portal：STA 连接失败 */

#endif // CAPTIVE_PORTAL_SERVICE_H
