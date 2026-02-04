/**
 * @file voice_service.c
 * @brief 语音模块命令服务实现 - 处理语音模块串口命令解析和分发
 */

#include "voice_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/uart/bsp_uart.h"
#include "../core/robot_mgr.h"
#include "../robot_common.h"
#include "soc_osal.h"

// ================= 配置参数 =================
#define VOICE_CMD_TIMEOUT_MS 10000  // 超时时间 (ms)
#define MOTOR_SPEED_HIGH 100        // 高速
#define MOTOR_SPEED_TURN 50         // 转向速度
#define TURN_DURATION_MS 400        // 转向持续时间 (ms)

// ================= 内部状态结构 =================
typedef struct {
  volatile bool active;                      // 命令是否有效
  volatile unsigned long long timeout_tick;  // 超时时刻
  volatile int8_t motor_l;                   // 左电机值
  volatile int8_t motor_r;                   // 右电机值
  uint8_t last_cmd;                          // 上一次收到的命令 (用于日志去重)

  // 转向恢复机制：转向完成后恢复到之前的状态
  bool is_turning;                   // 是否正在转向中
  unsigned long long turn_end_tick;  // 转向结束时刻
  int8_t saved_motor_l;              // 转向前保存的左电机值
  int8_t saved_motor_r;              // 转向前保存的右电机值
} VoiceCtrlBlock;

static VoiceCtrlBlock g_voice_cb = {0};

// ================= 辅助函数 =================

/**
 * @brief 设置期望速度 (辅助函数)
 */
static inline void set_speeds(int8_t l, int8_t r) {
  g_voice_cb.motor_l = l;
  g_voice_cb.motor_r = r;
  g_voice_cb.active = true;
  // 更新超时时间
  g_voice_cb.timeout_tick =
      osal_get_jiffies() + osal_msecs_to_jiffies(VOICE_CMD_TIMEOUT_MS);
}

/**
 * @brief 开始转向（保存当前状态并启动转向定时器）
 */
static void start_turn(int8_t turn_l, int8_t turn_r) {
  // 保存当前运动状态
  g_voice_cb.saved_motor_l = g_voice_cb.motor_l;
  g_voice_cb.saved_motor_r = g_voice_cb.motor_r;

  // 设置转向速度
  g_voice_cb.motor_l = turn_l;
  g_voice_cb.motor_r = turn_r;
  g_voice_cb.active = true;
  g_voice_cb.is_turning = true;
  g_voice_cb.turn_end_tick =
      osal_get_jiffies() + osal_msecs_to_jiffies(TURN_DURATION_MS);
  // 更新超时时间
  g_voice_cb.timeout_tick =
      osal_get_jiffies() + osal_msecs_to_jiffies(VOICE_CMD_TIMEOUT_MS);

  printf("[语音] 开始转向 (%d,%d)，保存状态 (%d,%d)\r\n", turn_l, turn_r,
         g_voice_cb.saved_motor_l, g_voice_cb.saved_motor_r);
}

// ================= 核心处理逻辑 =================

/**
 * @brief 解析并执行命令
 */
static void process_command(uint8_t cmd) {
  // 日志限流：只有命令改变时才打印，防止刷屏
  if (cmd != g_voice_cb.last_cmd) {
    printf("[语音] 命令变化 0x%02X\r\n", cmd);
    g_voice_cb.last_cmd = cmd;
  }

  // 0. 紧急停车命令（最高优先级，无论什么状态都执行）
  if (cmd == VOICE_CMD_EMERGENCY_STOP) {
    printf("[语音] 紧急停车！\r\n");
    g_voice_cb.motor_l = 0;
    g_voice_cb.motor_r = 0;
    g_voice_cb.active = false;
    g_voice_cb.is_turning = false;
    g_voice_cb.timeout_tick = 0;  // 清除超时时间
    g_voice_cb.last_cmd = 0xFF;   // 重置命令记录

    // 强制切换到遥控模式
    if (robot_mgr_get_status() != CAR_WIFI_CONTROL_STATUS) {
      robot_mgr_set_status(CAR_WIFI_CONTROL_STATUS);
    }
    return;
  }

  // 1. 模式切换指令 (0x10 ~ 0x1F)
  if (cmd >= 0x10 && cmd <= 0x1F) {
    CarStatus new_status = CAR_STOP_STATUS;
    switch (cmd) {
      case VOICE_CMD_STANDBY:
        new_status = CAR_STOP_STATUS;
        break;
      case VOICE_CMD_TRACE:
        new_status = CAR_TRACE_STATUS;
        break;
      case VOICE_CMD_OBSTACLE:
        new_status = CAR_OBSTACLE_AVOIDANCE_STATUS;
        break;
      case VOICE_CMD_REMOTE:
        new_status = CAR_WIFI_CONTROL_STATUS;
        break;
      default:
        return;  // 无效模式
    }

    // 切换模式时重置遥控状态
    g_voice_cb.active = false;
    g_voice_cb.is_turning = false;
    set_speeds(0, 0);

    // 执行切换
    if (robot_mgr_get_status() != new_status) {
      robot_mgr_set_status(new_status);
    }
    return;
  }

  // 2. 运动控制指令 (0x00 ~ 0x0F)
  // 收到运动指令，强制切入遥控模式
  if (robot_mgr_get_status() != CAR_WIFI_CONTROL_STATUS) {
    printf("[语音] 自动切换到遥控模式\r\n");
    robot_mgr_set_status(CAR_WIFI_CONTROL_STATUS);
  }

  switch (cmd) {
    case VOICE_CMD_STOP:
      set_speeds(0, 0);
      g_voice_cb.is_turning = false;  // 停止时清除转向状态
      break;
    case VOICE_CMD_FORWARD:
      set_speeds(MOTOR_SPEED_HIGH, MOTOR_SPEED_HIGH);
      g_voice_cb.is_turning = false;  // 前进时清除转向状态
      break;
    case VOICE_CMD_BACKWARD:
      set_speeds(-MOTOR_SPEED_HIGH, -MOTOR_SPEED_HIGH);
      g_voice_cb.is_turning = false;  // 后退时清除转向状态
      break;
    case VOICE_CMD_LEFT:
      // 启动定时转向，转向完成后恢复之前的状态
      start_turn(-MOTOR_SPEED_TURN, MOTOR_SPEED_TURN);
      break;
    case VOICE_CMD_RIGHT:
      // 启动定时转向，转向完成后恢复之前的状态
      start_turn(MOTOR_SPEED_TURN, -MOTOR_SPEED_TURN);
      break;
    default:
      break;
  }
}

/**
 * @brief 串口接收回调 (中断上下文)
 */
static void voice_rx_callback(const uint8_t* data, uint16_t length) {
  if (!data || length == 0) return;

  // 处理每一个字节，防止粘包
  for (uint16_t i = 0; i < length; i++) {
    process_command(data[i]);
  }
}

// ================= 对外接口 =================

void voice_service_init(void) {
  // 清空状态
  memset(&g_voice_cb, 0, sizeof(g_voice_cb));

  if (bsp_uart_init(voice_rx_callback) != 0) {
    printf("[语音] 初始化失败！\r\n");
    return;
  }
  printf("[语音] 服务已启动\r\n");
}

void voice_service_tick(void) {
  // 1. 转向定时检测
  if (g_voice_cb.is_turning) {
    if (osal_get_jiffies() >= g_voice_cb.turn_end_tick) {
      // 转向结束，恢复之前的状态
      printf("[语音] 转向结束，恢复状态 (%d,%d)\r\n", g_voice_cb.saved_motor_l,
             g_voice_cb.saved_motor_r);

      g_voice_cb.motor_l = g_voice_cb.saved_motor_l;
      g_voice_cb.motor_r = g_voice_cb.saved_motor_r;
      g_voice_cb.is_turning = false;

      // 如果恢复后是停止状态，清除 active 标志
      if (g_voice_cb.motor_l == 0 && g_voice_cb.motor_r == 0) {
        g_voice_cb.active = false;
      }
    }
  }

  // 2. 超时检测
  if (g_voice_cb.active) {
    if (osal_get_jiffies() >= g_voice_cb.timeout_tick) {
      printf("[语音] 超时，停止电机\r\n");
      g_voice_cb.active = false;
      g_voice_cb.is_turning = false;
      g_voice_cb.motor_l = 0;
      g_voice_cb.motor_r = 0;
      g_voice_cb.last_cmd = 0xFF;  // 重置上一条命令记录
    }
  }
}

bool voice_service_is_cmd_active(void) { return g_voice_cb.active; }

void voice_service_get_motor_cmd(int8_t* motor_l, int8_t* motor_r) {
  // 简单的原子性保护：一次性拷贝，避免读取期间被中断修改
  // 虽然int8是原子的，但两个变量之间可能存在不一致，struct copy相对安全
  if (g_voice_cb.active) {
    if (motor_l) *motor_l = g_voice_cb.motor_l;
    if (motor_r) *motor_r = g_voice_cb.motor_r;
  } else {
    if (motor_l) *motor_l = 0;
    if (motor_r) *motor_r = 0;
  }
}
