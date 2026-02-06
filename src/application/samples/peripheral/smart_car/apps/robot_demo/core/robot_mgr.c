#include "robot_mgr.h"

#include <stdbool.h>
#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../services/sle_service.h"
#include "../services/storage_service.h"
#include "../services/udp_service.h"
#include "../services/ui_service.h"
#include "mode_obstacle.h"
#include "mode_remote.h"
#include "mode_trace.h"
#include "robot_config.h"
#include "securec.h"
#include "soc_osal.h"

static CarStatus g_status = CAR_STOP_STATUS; /* 当前小车运行模式 */
static CarStatus g_last_status =
    CAR_STOP_STATUS; /* 上次小车运行模式（用于检测模式切换） */

// 全局机器人状态（包含距离、传感器值等）
static RobotState g_robot_state = {0};

// 互斥锁：保护 机器人状态，防止多个线程同时读写
static osal_mutex g_state_mutex;
// 互斥锁是否已初始化的标志（初始化成功后设为 true）
static bool g_state_mutex_inited = false;

static void mode_standby_enter(void) {
  // 切换到待机模式时，立即停止小车
  CAR_STOP();
}

/**
 * @brief 待机模式周期回调函数
 * @note 每 500ms 更新一次 OLED 显示，展示 WiFi 连接状态和 IP 地址
 */
static void mode_standby_tick(void) {
  static unsigned long long last_ui_update = 0;  // 上次 UI 更新时间戳
  unsigned long long now = osal_get_jiffies();   // 当前时间戳

  // 每 STANDBY_DELAY (500ms) 更新一次 UI，避免频繁刷新影响性能
  if (now - last_ui_update >= osal_msecs_to_jiffies(STANDBY_DELAY)) {
    char ip_line[BUF_IP] = {0};             // IP 地址显示缓冲区
    const char* ip = udp_service_get_ip();  // 从 UDP 服务获取 IP 地址

    // 格式化 IP 地址字符串（如果有 IP 就显示，否则显示 "Pending"）
    (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip ? ip : "Pending");

    WifiConnectStatus wifi_status =
        udp_service_get_wifi_status();        // 获取 WiFi 连接状态
    ui_render_standby(wifi_status, ip_line);  // 在 OLED 显示状态和 IP 地址
    last_ui_update = now;                     // 更新上次刷新时间戳
  }
}

static void mode_standby_exit(void) {
  // 退出待机模式时无需特殊处理
}

// 模式操作接口定义（按 CarStatus 枚举值索引）
static RobotModeOps g_mode_ops[] = {
    // CAR_STOP_STATUS (0)
    {mode_standby_enter, mode_standby_tick, mode_standby_exit},
    // CAR_TRACE_STATUS (1)
    {mode_trace_enter, mode_trace_tick, mode_trace_exit},
    // CAR_OBSTACLE_AVOIDANCE_STATUS (2)
    {mode_obstacle_enter, mode_obstacle_tick, mode_obstacle_exit},
    // CAR_WIFI_CONTROL_STATUS (3)
    {mode_remote_enter, mode_remote_tick, mode_remote_exit}};

/**
 * @brief 初始化状态互斥锁，保护全局机器人状态的并发访问
 */
static void robot_mgr_state_mutex_init(void) {
  if (g_state_mutex_inited) return;

  if (osal_mutex_init(&g_state_mutex) == OSAL_SUCCESS)
    g_state_mutex_inited = true;
  else
    printf("RobotMgr: 状态互斥锁初始化失败\r\n");
}

/**
 * @brief 初始化机器人管理器，包括所有硬件驱动和服务
 * @note 初始化电机、超声波、红外驱动，以及网络、UI、HTTP 服务
 */
void robot_mgr_init(void) {
  // 优先加载运行参数，供后续模式逻辑读取（避障阈值等）
  storage_service_init();

  l9110s_init();
  hcsr04_init();
  tcrt5000_adc_init();  // 使用ADC模式初始化TCRT5000

  ui_service_init();
  udp_service_init();
  sle_service_init();
  robot_mgr_state_mutex_init();
  robot_mgr_set_status(CAR_STOP_STATUS);
  g_last_status = CAR_STOP_STATUS;

  printf("RobotMgr: 初始化完成\r\n");
}

/**
 * @brief 获取当前小车状态
 * @return 当前状态枚举值（停止、循迹、避障、WiFi 控制等）
 */
CarStatus robot_mgr_get_status(void) { return g_status; }

/**
 * @brief 设置小车状态并更新 UI 显示
 * @param status 新的状态值
 */
void robot_mgr_set_status(CarStatus status) {
  if (g_status != status) {
    g_status = status;
    // 加锁保护状态更新
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_robot_state.mode = status;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
    ui_show_mode_page(status);
  }
}

/**
 * @brief 周期性调用函数，处理模式生命周期和状态机
 */
void robot_mgr_tick(void) {
  CarStatus current_status = g_status;  // 当前状态
  int mode_count =
      (int)(sizeof(g_mode_ops) / sizeof(g_mode_ops[0]));  // 模式数量

  // 1. 处理状态切换
  if (current_status != g_last_status) {
    // 退出旧模式
    if (g_last_status >= CAR_STOP_STATUS && g_last_status < mode_count) {
      if (g_mode_ops[g_last_status].exit) g_mode_ops[g_last_status].exit();
    }

    // 进入新模式
    if (current_status >= CAR_STOP_STATUS && current_status < mode_count) {
      if (g_mode_ops[current_status].enter) g_mode_ops[current_status].enter();
    }

    g_last_status = current_status;
  }

  // 2. 执行当前模式逻辑
  if (current_status >= CAR_STOP_STATUS && current_status < mode_count) {
    if (g_mode_ops[current_status].tick) g_mode_ops[current_status].tick();
  }
}

/**
 * @brief 获取全局机器人状态的副本
 * @param out 输出参数，用于接收状态副本
 * @note 此函数线程安全，使用互斥锁保护数据读取
 */
void robot_mgr_get_state_copy(RobotState* out) {
  if (out == NULL) return;

  // 加锁保护状态读取
  MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
  *out = g_robot_state;
  MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

/**
 * @brief 更新超声波测距值到全局状态
 * @param distance 距离值（单位：厘米）
 * @note 使用互斥锁保护，防止多个线程同时修改状态
 */
void robot_mgr_update_distance(float distance) {
  MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
  g_robot_state.distance = distance;
  MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

/**
 * @brief 更新红外传感器状态到全局状态
 * @param left 左侧红外传感器状态
 * @param middle 中间红外传感器状态
 * @param right 右侧红外传感器状态
 * @note 使用互斥锁保护，防止多个线程同时修改状态
 */
void robot_mgr_update_ir_status(unsigned int left, unsigned int middle,
                                unsigned int right) {
  MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
  g_robot_state.ir_left = left;
  g_robot_state.ir_middle = middle;
  g_robot_state.ir_right = right;
  MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}
