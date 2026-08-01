/**
 * @file voice_channel.h
 * @brief 语音（UART）控制通道 - 协议定义
 *
 * UART配置:
 *   - 波特率: 9600
 *   - 数据位: 8
 *   - 停止位: 1
 *
 * 协议格式: 单字节命令
 */

#ifndef VOICE_CHANNEL_H
#define VOICE_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 协议定义 (单字节命令)
 * 0x00-0x0F: 运动控制
 * 0x10-0x1F: 模式切换
 */
typedef enum {
    // 运动控制 (0x00-0x0F)
    VOICE_CMD_STOP = 0x00,     // 停止
    VOICE_CMD_FORWARD = 0x01,  // 前进 (持续1000ms)
    VOICE_CMD_BACKWARD = 0x02, // 后退 (持续1000ms)
    VOICE_CMD_LEFT = 0x03,     // 左转 (持续400ms后自动停止)
    VOICE_CMD_RIGHT = 0x04,    // 右转 (持续400ms后自动停止)

    // 模式切换 (0x10-0x1F)
    VOICE_CMD_STANDBY = 0x10,  // 待机模式
    VOICE_CMD_TRACE = 0x11,    // 循迹模式
    VOICE_CMD_OBSTACLE = 0x12, // 避障模式
    VOICE_CMD_REMOTE = 0x13    // 遥控模式
} VoiceCommand;

// 语音（UART）控制通道：串口字节 → 运动/模式意图 → 投递控制中枢
void voice_channel_init(void); // 初始化语音通道（注册 UART 回调）

#endif // VOICE_CHANNEL_H
