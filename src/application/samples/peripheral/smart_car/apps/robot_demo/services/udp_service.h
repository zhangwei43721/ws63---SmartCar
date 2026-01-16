#ifndef UDP_SERVICE_H
#define UDP_SERVICE_H

#include "../core/robot_mgr.h"
#include "../core/robot_config.h"

#include "securec.h"
#include "soc_osal.h"

#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UDP_SERVER_PORT 8888
#define UDP_SERVER_IP "192.168.3.151"
#define UDP_STACK_SIZE 4096
#define UDP_TASK_PRIORITY 24

#define UDP_BUFFER_SIZE 256
#define UDP_BROADCAST_PORT 8889

// UDP数据包格式 (7字节)
typedef struct {
    uint8_t type;        // 数据包类型: 0x01=控制, 0x02=状态, 0x03=模式, 0xFE=心跳, 0xFF=存在广播
    uint8_t cmd;         // 命令类型/模式编号
    int8_t motor1;       // 左电机值 -100~100
    int8_t motor2;       // 右电机值 -100~100
    int8_t servo;        // 舵机角度 0~180
    int8_t ir_data;      // 红外传感器数据 (bit0=左, bit1=中, bit2=右)
    uint8_t checksum;    // 校验和（累加和）
} __attribute__((packed)) udp_packet_t;

void udp_service_init(void);
bool udp_service_is_connected(void);
const char *udp_service_get_ip(void);
void udp_service_send_state(void);
bool udp_service_pop_cmd(int8_t *motor1_out, int8_t *motor2_out, int8_t *servo_out);
void udp_service_push_cmd(int8_t motor1, int8_t motor2, int8_t servo);

#endif