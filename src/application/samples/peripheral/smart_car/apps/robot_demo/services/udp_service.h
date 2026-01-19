#ifndef UDP_SERVICE_H
#define UDP_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "errcode.h"

#define UDP_SERVER_PORT 8888
#define UDP_SERVER_IP "192.168.3.151"
#define UDP_STACK_SIZE 4096
#define UDP_TASK_PRIORITY 24

#define UDP_BUFFER_SIZE 2048
#define UDP_BROADCAST_PORT 8889

typedef struct {
    errcode_t (*prepare)(void *ctx, uint32_t total_size);
    errcode_t (*write)(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len);
    errcode_t (*finish)(void *ctx);
    errcode_t (*reset)(void *ctx);
} udp_ota_backend_t;

void udp_service_init(void);
bool udp_service_is_connected(void);
const char *udp_service_get_ip(void);
void udp_service_send_state(void);
bool udp_service_pop_cmd(int8_t *motor1_out, int8_t *motor2_out, int8_t *servo_out);
void udp_service_push_cmd(int8_t motor1, int8_t motor2, int8_t servo);
void udp_service_register_ota_backend(const udp_ota_backend_t *backend, void *ctx);

#endif
