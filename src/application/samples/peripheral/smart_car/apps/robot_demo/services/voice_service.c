/**
 * @file voice_service.c
 * @brief 语音模块命令服务实现 - 处理语音模块串口命令解析和分发
 */

#include "voice_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/uart/bsp_uart.h"
#include "../core/robot_mgr.h"
#include "soc_osal.h"

#define VOICE_CMD_TIMEOUT_MS 1000
#define MOTOR_SPEED_HIGH 100
#define MOTOR_SPEED_TURN 50
#define TURN_DURATION_MS 400  // 转向持续时间

typedef struct {
  int8_t l, r;
  unsigned long long expire;  // 指令到期时刻
} VoiceCtx;

static VoiceCtx g_ctx = {0};

// 统一设置动作：速度(l, r)，持续时间(ms)
static void set_motion(int8_t l, int8_t r, uint32_t ms) {
  g_ctx.l = l;  // 左轮速度
  g_ctx.r = r;  // 右轮速度
  g_ctx.expire = osal_get_jiffies() + osal_msecs_to_jiffies(ms);
}

static void process_command(uint8_t cmd) {
  // 1. 模式切换 (0x10-0x1F)
  if (cmd >= 0x10) {
    static const CarStatus modes[] = {CAR_STOP_STATUS, CAR_TRACE_STATUS,
                                      CAR_OBSTACLE_AVOIDANCE_STATUS,
                                      CAR_WIFI_CONTROL_STATUS};
    if (cmd - 0x10 < 4) {
      set_motion(0, 0, 0);
      robot_mgr_set_status(modes[cmd - 0x10]);
    }
    return;
  }

  // 2. 运动控制：强制切入遥控模式
  if (robot_mgr_get_status() != CAR_WIFI_CONTROL_STATUS)
    robot_mgr_set_status(CAR_WIFI_CONTROL_STATUS);

  switch (cmd) {
    case VOICE_CMD_STOP:
      set_motion(0, 0, 0);
      break;
    case VOICE_CMD_FORWARD:
      set_motion(MOTOR_SPEED_HIGH, MOTOR_SPEED_HIGH, VOICE_CMD_TIMEOUT_MS);
      break;
    case VOICE_CMD_BACKWARD:
      set_motion(-MOTOR_SPEED_HIGH, -MOTOR_SPEED_HIGH, VOICE_CMD_TIMEOUT_MS);
      break;
    case VOICE_CMD_LEFT:
      set_motion(-MOTOR_SPEED_TURN, MOTOR_SPEED_TURN, TURN_DURATION_MS);
      break;
    case VOICE_CMD_RIGHT:
      set_motion(MOTOR_SPEED_TURN, -MOTOR_SPEED_TURN, TURN_DURATION_MS);
      break;
  }
}

// UART接收回调
static void voice_rx_callback(const uint8_t* data, uint16_t length) {
  if (!data || length == 0) return;
  for (uint16_t i = 0; i < length; i++) {
    process_command(data[i]);
  }
}

void voice_service_init(void) {
  memset(&g_ctx, 0, sizeof(g_ctx));
  if (bsp_uart_init(voice_rx_callback) != 0) {
    printf("[语音] 初始化失败！\r\n");
    return;
  }
  printf("[语音] 服务已启动\r\n");
}

void voice_service_tick(void) {
  // 检查是否有有效的语音命令
  if (g_ctx.l != 0 || g_ctx.r != 0) {
    // 检查是否到期
    if (osal_get_jiffies() >= g_ctx.expire) {
      g_ctx.l = 0;
      g_ctx.r = 0;
      // 语音命令到期，立即停车
      l9110s_set_differential(0, 0);
    } else {
      // 命令有效期内，持续执行电机驱动
      l9110s_set_differential(g_ctx.l, g_ctx.r);
    }
  }
}

bool voice_service_is_cmd_active(void) {
  return (g_ctx.l != 0 || g_ctx.r != 0);
}

void voice_service_get_motor_cmd(int8_t* l, int8_t* r) {
  if (l) *l = g_ctx.l;
  if (r) *r = g_ctx.r;
}
