/**
 * @file voice_channel.c
 * @brief 语音（UART）控制通道实现 - 纯翻译层：无任务、无队列、无定时器
 *
 * 设计：
 *  - UART 回调（BSP UART ISR 上下文）把语音命令字节直接翻译成标准
 *    car_packet_t 协议包，经 car_ctrl_post_cmd 投到统一命令总线；
 *    翻译是纯查表 + NO_WAIT 队列写，无锁无阻塞，ISR-safe
 *  - 消费由 car_ctrl 命令总线任务统一承担，本通道不再创建私有任务
 *    （任务上限吃紧，曾因任务池耗尽导致 voice_task 创建失败、语音全程无响应）
 *  - 超时停车不再由本通道维护：bsp_motor 400ms 看门狗对所有遥控通道
 *    统一兜底，与 UDP/SLE 遥控行为一致
 */

#include "voice_channel.h"

#include <stdio.h>

#include "../../../drivers/uart/bsp_uart.h"
#include "../car_common.h"
#include "../core/car_ctrl.h"

// 模式切换表：cmd 0x10..0x13 对应索引
static const CarStatus g_mode_table[] = {
    CAR_STOP_STATUS,
    CAR_TRACE_STATUS,
    CAR_OBSTACLE_AVOIDANCE_STATUS,
    CAR_WIFI_CONTROL_STATUS,
};
#define VOICE_MODE_CMD_BASE 0x10                                              // 模式切换命令起始值
#define VOICE_MODE_CMD_COUNT (sizeof(g_mode_table) / sizeof(g_mode_table[0])) // 模式表条目数

// 投递一个标准协议包到控制中枢总线（ISR-safe：数据整体拷贝，NO_WAIT 队列写）
static void voice_post_packet(uint8_t type, uint8_t cmd, int8_t l, int8_t r)
{
    car_cmd_t msg = {
        .source = MODE_SRC_VOICE,
        .reply = NULL,
        .reply_ctx = NULL,
        .len = sizeof(car_packet_t),
        .data = {type, cmd, (uint8_t)l, (uint8_t)r, 0},
    };
    (void)car_ctrl_post_cmd(&msg);
}

// UART 回调：字节翻译为标准协议包直投总线，不做任何业务决策
static void voice_rx_callback(const uint8_t *data, uint16_t length)
{
    if (!data)
        return;

    for (uint16_t i = 0; i < length; i++) {
        uint8_t cmd = data[i];

        if (cmd >= VOICE_MODE_CMD_BASE && cmd < VOICE_MODE_CMD_BASE + VOICE_MODE_CMD_COUNT) {
            // 模式切换前先投停车包（防御性；非遥控模式下会被安全网关拦截，无副作用）
            voice_post_packet(CAR_PKT_CONTROL, 0, 0, 0);
            voice_post_packet(CAR_PKT_MODE, (uint8_t)g_mode_table[cmd - VOICE_MODE_CMD_BASE], 0, 0);
            continue;
        }

        // 运动命令
        if (cmd <= VOICE_CMD_RIGHT) {
            voice_post_packet(CAR_PKT_CONTROL, cmd, 0, 0);
        }
    }
}

// 初始化语音通道：注册 UART 接收回调（本通道不占用任务/队列/定时器资源）
void voice_channel_init(void)
{
    if (bsp_uart_init(voice_rx_callback) != 0) {
        printf("[语音] UART 初始化失败！\r\n");
        return;
    }

    printf("[语音] 通道已启动\r\n");
}
