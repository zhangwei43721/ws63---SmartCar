/**
 * @file uart_service.c
 * @brief UART命令服务实现 - 处理串口命令解析和分发
 */

#include "uart_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/uart/bsp_uart.h"
#include "../core/robot_mgr.h"
#include "../robot_common.h"
#include "soc_osal.h"

// ================= 配置参数 =================
#define UART_CMD_TIMEOUT_MS 1000  // 超时时间 (ms)
#define MOTOR_SPEED_HIGH 100      // 高速
#define MOTOR_SPEED_TURN 50       // 转向速度

// ================= 内部状态结构 =================
typedef struct {
  volatile bool active;                      // 命令是否有效
  volatile unsigned long long timeout_tick;  // 超时时刻
  volatile int8_t motor_l;                   // 左电机值
  volatile int8_t motor_r;                   // 右电机值
  uint8_t last_cmd;                          // 上一次收到的命令 (用于日志去重)
} UartCtrlBlock;

static UartCtrlBlock g_uart_cb = {0};

// ================= 辅助函数 =================

/**
 * @brief 设置期望速度 (辅助函数)
 */
static inline void set_speeds(int8_t l, int8_t r) {
  g_uart_cb.motor_l = l;
  g_uart_cb.motor_r = r;
  g_uart_cb.active = true;
  // 更新超时时间
  g_uart_cb.timeout_tick =
      osal_get_jiffies() + osal_msecs_to_jiffies(UART_CMD_TIMEOUT_MS);
}

// ================= 核心处理逻辑 =================

/**
 * @brief 解析并执行命令
 */
static void process_command(uint8_t cmd) {
  // 日志限流：只有命令改变时才打印，防止刷屏
  if (cmd != g_uart_cb.last_cmd) {
    printf("UART: Cmd change 0x%02X\r\n", cmd);
    g_uart_cb.last_cmd = cmd;
  }

  // 1. 模式切换指令 (0x10 ~ 0x1F)
  if (cmd >= 0x10 && cmd <= 0x1F) {
    CarStatus new_status = CAR_STOP_STATUS;
    switch (cmd) {
      case UART_CMD_STANDBY:
        new_status = CAR_STOP_STATUS;
        break;
      case UART_CMD_TRACE:
        new_status = CAR_TRACE_STATUS;
        break;
      case UART_CMD_OBSTACLE:
        new_status = CAR_OBSTACLE_AVOIDANCE_STATUS;
        break;
      case UART_CMD_REMOTE:
        new_status = CAR_WIFI_CONTROL_STATUS;
        break;
      default:
        return;  // 无效模式
    }

    // 切换模式时重置遥控状态
    g_uart_cb.active = false;
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
    printf("UART: Auto-switch to REMOTE\r\n");
    robot_mgr_set_status(CAR_WIFI_CONTROL_STATUS);
  }

  switch (cmd) {
    case UART_CMD_STOP:
      set_speeds(0, 0);
      break;
    case UART_CMD_FORWARD:
      set_speeds(MOTOR_SPEED_HIGH, MOTOR_SPEED_HIGH);
      break;
    case UART_CMD_BACKWARD:
      set_speeds(-MOTOR_SPEED_HIGH, -MOTOR_SPEED_HIGH);
      break;
    case UART_CMD_LEFT:
      set_speeds(-MOTOR_SPEED_TURN, MOTOR_SPEED_TURN);
      break;
    case UART_CMD_RIGHT:
      set_speeds(MOTOR_SPEED_TURN, -MOTOR_SPEED_TURN);
      break;
    default:
      break;
  }
}

/**
 * @brief UART接收回调 (中断上下文)
 */
static void uart_rx_callback(const uint8_t* data, uint16_t length) {
  if (!data || length == 0) return;

  // 处理每一个字节，防止粘包
  for (uint16_t i = 0; i < length; i++) {
    process_command(data[i]);
  }
}

// ================= 对外接口 =================

void uart_service_init(void) {
  // 清空状态
  memset(&g_uart_cb, 0, sizeof(g_uart_cb));

  if (bsp_uart_init(uart_rx_callback) != 0) {
    printf("UART: Init failed!\r\n");
    return;
  }
  printf("UART: Service started.\r\n");
}

void uart_service_tick(void) {
  // 超时检测
  if (g_uart_cb.active) {
    if (osal_get_jiffies() >= g_uart_cb.timeout_tick) {
      printf("UART: Timeout, stop motors\r\n");
      g_uart_cb.active = false;
      g_uart_cb.motor_l = 0;
      g_uart_cb.motor_r = 0;
      g_uart_cb.last_cmd = 0xFF;  // 重置上一条命令记录
    }
  }
}

bool uart_service_is_cmd_active(void) { return g_uart_cb.active; }

void uart_service_get_motor_cmd(int8_t* motor_l, int8_t* motor_r) {
  // 简单的原子性保护：一次性拷贝，避免读取期间被中断修改
  // 虽然int8是原子的，但两个变量之间可能存在不一致，struct copy相对安全
  if (g_uart_cb.active) {
    if (motor_l) *motor_l = g_uart_cb.motor_l;
    if (motor_r) *motor_r = g_uart_cb.motor_r;
  } else {
    if (motor_l) *motor_l = 0;
    if (motor_r) *motor_r = 0;
  }
}