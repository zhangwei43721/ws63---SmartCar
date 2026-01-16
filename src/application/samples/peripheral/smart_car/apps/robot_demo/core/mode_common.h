/**
 ****************************************************************************************************
 * @file        mode_common.h
 * @brief       模式运行通用框架接口
 * @note        提供统一模式运行循环和遥测发送功能
 ****************************************************************************************************
 */

#ifndef MODE_COMMON_H
#define MODE_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "soc_osal.h"
#include <stdbool.h>
#include <stddef.h>

/* 模式运行上下文 */
typedef struct {
    unsigned long long enter_time;          // 模式进入时间
    unsigned long long last_telemetry_time; // 上次遥测时间
    bool is_running;                         // 运行状态标志
} ModeContext;

/* 模式函数指针类型 */
typedef void (*ModeRunFunc)(ModeContext *ctx);
typedef void (*ModeExitFunc)(ModeContext *ctx);

/**
 * @brief 通用模式运行框架
 * @param expected_status 期望的模式状态
 * @param run_func 模式运行函数指针
 * @param exit_func 模式退出函数指针（可为NULL）
 * @param telemetry_interval_ms 遥测上报间隔（毫秒）
 */
void mode_run_loop(int expected_status, ModeRunFunc run_func, ModeExitFunc exit_func,
                   unsigned int telemetry_interval_ms);

/**
 * @brief 发送遥测数据
 * @param mode 模式名称
 * @param data JSON格式的额外数据
 */
void send_telemetry(const char *mode, const char *data);

#ifdef __cplusplus
}
#endif

#endif /* MODE_COMMON_H */
