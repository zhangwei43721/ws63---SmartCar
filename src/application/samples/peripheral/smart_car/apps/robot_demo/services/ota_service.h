/**
 * @file        ota_service.h
 * @brief       局域网 OTA 远程烧录服务头文件
 * @details     UDP 触发后启动 TCP 服务端接收固件，写入 UPG 暂存区后重启升级
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "errcode.h"

#define OTA_TCP_PORT 8890
#define OTA_TCP_STACK_SIZE (1024 * 16) // 16KB 栈，TCP+UPG 操作需要较大空间
#define OTA_TCP_TASK_PRIORITY 23

#define OTA_RECV_CHUNK_SIZE 32768  // 每次 recv 缓冲大小

#define OTA_MAGIC_STR "OTAx"
#define OTA_MAGIC_LEN 4

// OTA 状态机：IDLE → WAITING(监听TCP) → RECEIVING → VERIFYING → UPGRADING(重启)
// 任一阶段异常 → FAILED → 500ms 定时器自动回到 IDLE
#define OTA_STATE_MAP(OP)                \
    OP(OTA_STATE_IDLE, "IDLE")           \
    OP(OTA_STATE_WAITING, "WAITING")     \
    OP(OTA_STATE_RECEIVING, "RECEIVING") \
    OP(OTA_STATE_VERIFYING, "VERIFYING") \
    OP(OTA_STATE_UPGRADING, "UPGRADING") \
    OP(OTA_STATE_FAILED, "FAILED")

#define OTA_STATE_ENUM(s, str) s,
#define OTA_STATE_STR(s, str) str,

typedef enum { OTA_STATE_MAP(OTA_STATE_ENUM) OTA_STATE_MAX } ota_state_t;

void ota_service_init(void);

/**
 * @brief 启动 OTA TCP 接收流程（由 UDP 触发调用）
 * @param expected_size 预期固件大小（0 表示未知，等 TCP header 告知）
 * @return true 成功启动监听任务，false 失败或已在进行中
 */
bool ota_service_start(uint32_t expected_size);

void ota_service_cancel(void);

#endif // OTA_SERVICE_H
