#include "robot_mgr.h"

#include <stdbool.h>
#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../services/captive_portal_service.h"
#include "../services/ota_service.h"
#include "../services/sle_service.h"
#include "../services/storage_service.h"
#include "../services/udp_service.h"
#include "../services/ui_service.h"
#include "mode_obstacle.h"
#include "mode_trace.h"
#include "motor_executor.h"
#include "robot_config.h"
#include "sensor_task.h"
#include "securec.h"
#include "soc_osal.h"

static CarStatus g_status = CAR_STOP_STATUS; /* 当前小车运行模式 */
static CarStatus g_last_status =
    CAR_STOP_STATUS; /* 上次小车运行模式（用于检测模式切换） */

/* ---------- 模式命令消息队列（生产者-消费者模型） ---------- */
typedef struct {
  CarStatus status;
  uint32_t source;
} ModeCmdMsg;

#define MODE_CMD_QUEUE_DEPTH 4
static unsigned long g_mode_queue = 0;
static bool g_mode_queue_inited = false;

/* robot_mgr 任务参数 */
#define ROBOT_MGR_TASK_STACK_SIZE (1024 * 4)
#define ROBOT_MGR_TASK_PRIO       25
#define ROBOT_MGR_TICK_MS         20
static osal_task *g_robot_mgr_task = NULL;
static void robot_mgr_task_start(void);
static void robot_mgr_apply_status(CarStatus status);

// 全局机器人状态（包含距离、传感器值等）
static RobotState g_robot_state = {0};

// 互斥锁：保护 机器人状态，防止多个线程同时读写
static osal_mutex g_state_mutex;
// 互斥锁是否已初始化的标志（初始化成功后设为 true）
static bool g_state_mutex_inited = false;

/* 遥控模式：电机由 UDP/HTTP/SLE 各服务直接推送到 Motor 队列，
 * 400ms 无新命令时 Motor Executor 自动停车，无需主循环逻辑。 */
static void mode_remote_enter(void) {
  printf("Robot: 遥控模式\r\n");
  motor_executor_push_cmd(0, 0);
}
static void mode_remote_exit(void) {
  motor_executor_push_cmd(0, 0);
}

static void mode_standby_enter(void) {
  // 切换到待机模式时，立即停止小车
  motor_executor_push_cmd(0, 0);
}

/**
 * @brief 待机模式周期回调函数
 * @note 每 500ms 更新一次 OLED 显示，展示 WiFi 连接状态和 IP 地址
 */
static void mode_standby_tick(void) {
  static unsigned long long last_ui_update = 0;
  unsigned long long now = osal_get_jiffies();

  if (now - last_ui_update >= osal_msecs_to_jiffies(STANDBY_DELAY)) {
    char ip_line[BUF_IP] = {0};
    WifiConnectStatus wifi_status = udp_service_get_wifi_status();

    if (wifi_status == WIFI_STATUS_AP_MODE) {
      const char* ap_ip = captive_portal_service_get_ap_ip();
      (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ap_ip);
    } else {
      const char* ip = udp_service_get_ip();
      (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip ? ip : "Pending");
    }

    ui_render_standby(wifi_status, ip_line);
    last_ui_update = now;
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
    {mode_trace_enter, NULL, mode_trace_exit},
    // CAR_OBSTACLE_AVOIDANCE_STATUS (2)
    {mode_obstacle_enter, NULL, mode_obstacle_exit},
    // CAR_WIFI_CONTROL_STATUS (3)
    {mode_remote_enter, NULL, mode_remote_exit}};

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
  ota_service_init();
  sle_service_init();
  captive_portal_service_init();
  sensor_task_init();
  robot_mgr_state_mutex_init();

  /* 创建模式命令队列 */
  if (!g_mode_queue_inited) {
    if (osal_msg_queue_create("mode_q", MODE_CMD_QUEUE_DEPTH, &g_mode_queue, 0,
                              sizeof(ModeCmdMsg)) == OSAL_SUCCESS) {
      g_mode_queue_inited = true;
    } else {
      printf("RobotMgr: 模式队列创建失败\r\n");
    }
  }

  robot_mgr_apply_status(CAR_STOP_STATUS);
  g_last_status = CAR_STOP_STATUS;
  /* 强制刷一次首屏（apply_status 在状态相同时会短路） */
  ui_show_mode_page(CAR_STOP_STATUS);

  motor_executor_init();

  robot_mgr_task_start();

  printf("RobotMgr: 初始化完成\r\n");
  printf("[FIRMWARE] OTA_TEST_BUILD_20250519_V2\r\n");
}

/**
 * @brief 获取当前小车状态
 * @return 当前状态枚举值（停止、循迹、避障、WiFi 控制等）
 */
CarStatus robot_mgr_get_status(void) { return g_status; }

/**
 * @brief 内部使用：在任务上下文执行真正的状态切换 + 全局状态写入 + UI 投递。
 *        外部生产者应通过 robot_mgr_post_mode 投递队列，避免 ISR 不安全操作。
 */
static void robot_mgr_apply_status(CarStatus status) {
  if (g_status == status) return;

  const char* mode_names[] = {"停止", "循迹", "避障", "遥控"};
  printf("模式切换：%s -> %s\r\n", mode_names[g_status], mode_names[status]);

  g_status = status;
  MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
  g_robot_state.mode = status;
  MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
  ui_show_mode_page(status);
}

/**
 * @brief 生产者-消费者投递接口：ISR 与任务上下文均可调用。
 *        全程 NO_WAIT；队列满时丢弃最旧请求，保留最新意图。
 */
bool robot_mgr_post_mode(CarStatus status, uint32_t source) {
  if (!g_mode_queue_inited) return false;

  ModeCmdMsg msg = { .status = status, .source = source };

  uint32_t irq_sts = osal_irq_lock();
  if (osal_msg_queue_get_msg_num(g_mode_queue) >= MODE_CMD_QUEUE_DEPTH) {
    ModeCmdMsg dummy;
    unsigned int sz = sizeof(dummy);
    (void)osal_msg_queue_read_copy(g_mode_queue, &dummy, &sz,
                                   OSAL_MSGQ_NO_WAIT);
  }
  int ret = osal_msg_queue_write_copy(g_mode_queue, &msg, sizeof(msg),
                                      OSAL_MSGQ_NO_WAIT);
  osal_irq_restore(irq_sts);

  return (ret == OSAL_SUCCESS);
}

/**
 * @brief 周期性调用函数，处理模式生命周期和状态机
 */
void robot_mgr_tick(void) {
  /* 0. 处理生产者投递的模式切换请求（队列里可能堆积多条，只保留最后一条意图） */
  if (g_mode_queue_inited) {
    ModeCmdMsg msg;
    unsigned int sz = sizeof(msg);
    while (osal_msg_queue_read_copy(g_mode_queue, &msg, &sz,
                                    OSAL_MSGQ_NO_WAIT) == OSAL_SUCCESS) {
      robot_mgr_apply_status(msg.status);
      sz = sizeof(msg);
    }
  }

  CarStatus current_status = g_status;  // 当前状态
  size_t mode_count = sizeof(g_mode_ops) / sizeof(g_mode_ops[0]);

  _Static_assert(CAR_WIFI_CONTROL_STATUS + 1 == 4, "CarStatus enum mismatch");

  // 1. 处理状态切换
  if (current_status != g_last_status) {
    // 退出旧模式（exit 中自行负责停车）
    if (g_last_status >= CAR_STOP_STATUS && (size_t)g_last_status < mode_count) {
      if (g_mode_ops[g_last_status].exit) g_mode_ops[g_last_status].exit();
    }

    // 进入新模式
    if (current_status >= CAR_STOP_STATUS && (size_t)current_status < mode_count) {
      if (g_mode_ops[current_status].enter) g_mode_ops[current_status].enter();
    }

    g_last_status = current_status;
  }

  // 2. 执行当前模式逻辑
  if (current_status >= CAR_STOP_STATUS && (size_t)current_status < mode_count) {
    if (g_mode_ops[current_status].tick) g_mode_ops[current_status].tick();
  }
}

/* robot_mgr 独立任务：周期驱动模式生命周期与 tick */
static int robot_mgr_task_entry(void *arg) {
  (void)arg;
  printf("RobotMgr: 状态机任务启动\r\n");
  while (1) {
    robot_mgr_tick();
    osal_msleep(ROBOT_MGR_TICK_MS);
  }
  return 0;
}

static void robot_mgr_task_start(void) {
  if (g_robot_mgr_task != NULL) return;
  osal_kthread_lock();
  g_robot_mgr_task = osal_kthread_create(
      (osal_kthread_handler)robot_mgr_task_entry, NULL, "robot_mgr",
      ROBOT_MGR_TASK_STACK_SIZE);
  if (g_robot_mgr_task != NULL) {
    osal_kthread_set_priority(g_robot_mgr_task, ROBOT_MGR_TASK_PRIO);
  }
  osal_kthread_unlock();
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

/* motor_executor.c 提供统一的电机命令队列 */
