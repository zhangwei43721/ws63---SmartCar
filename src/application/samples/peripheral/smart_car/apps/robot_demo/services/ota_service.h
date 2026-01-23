#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "errcode.h"
#include "lwip/sockets.h"

// OTA 后端接口定义
typedef struct {
    errcode_t (*prepare)(void *ctx, uint32_t total_size); // 准备升级环境，预留存储空间
    errcode_t (*write)(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len); // 写入升级数据
    errcode_t (*finish)(void *ctx); // 完成升级，触发设备重启
    errcode_t (*reset)(void *ctx);  // 重置升级状态
} ota_backend_t;

/**
 * @brief 初始化 OTA 服务
 */
void ota_service_init(void);

/**
 * @brief 处理接收到的 UDP 数据包，如果是 OTA 相关包则处理之
 * @param data 接收到的数据指针
 * @param len 数据长度
 * @param from_addr 发送方地址
 * @return true 如果是 OTA 数据包并已处理，false 如果不是 OTA 数据包
 */
bool ota_service_process_packet(const uint8_t *data, size_t len, const struct sockaddr_in *from_addr);

/**
 * @brief 注册自定义 OTA 后端
 * @param backend 后端接口指针
 * @param ctx 上下文指针
 */
void ota_service_register_backend(const ota_backend_t *backend, void *ctx);

#endif
