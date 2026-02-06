/**
 * @file voice_service.h
 * @brief 语音模块命令服务 - 协议定义
 *
 * UART配置:
 *   - 波特率: 9600
 *   - 数据位: 8
 *   - 停止位: 1
 *
 * 协议格式: 单字节命令
 */

#ifndef VOICE_SERVICE_H
#define VOICE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 协议定义 (单字节命令)
 * 0x00-0x0F: 运动控制
 * 0x10-0x1F: 模式切换
 */
typedef enum {
  /* 运动控制 (0x00-0x0F) */
  VOICE_CMD_STOP = 0x00,      // 停止
  VOICE_CMD_FORWARD = 0x01,   // 前进 (持续1000ms)
  VOICE_CMD_BACKWARD = 0x02,  // 后退 (持续1000ms)
  VOICE_CMD_LEFT = 0x03,      // 左转 (持续400ms后自动停止)
  VOICE_CMD_RIGHT = 0x04,     // 右转 (持续400ms后自动停止)

  /* 模式切换 (0x10-0x1F) */
  VOICE_CMD_STANDBY = 0x10,   // 待机模式
  VOICE_CMD_TRACE = 0x11,     // 循迹模式
  VOICE_CMD_OBSTACLE = 0x12,  // 避障模式
  VOICE_CMD_REMOTE = 0x13     // 遥控模式
} VoiceCommand;

void voice_service_init(void);
void voice_service_tick(void);
bool voice_service_is_cmd_active(void);
void voice_service_get_motor_cmd(int8_t* motor_l, int8_t* motor_r);

#endif /* VOICE_SERVICE_H */
