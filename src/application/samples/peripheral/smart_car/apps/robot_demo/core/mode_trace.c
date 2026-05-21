#include "mode_trace.h"

#include <stdio.h>

#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../services/storage_service.h"
#include "adc.h"
#include "motor_executor.h"
#include "robot_config.h"
#include "robot_mgr.h"
#include "soc_osal.h"

#define TRACE_SPEED_FORWARD 40
#define TRACE_LOST_TIMEOUT_MS 300
#define TRACE_SEARCH_SPEED 30
#define TRACE_TICK_MS 20

#define TRACE_TASK_STACK_SIZE 2048
#define TRACE_TASK_PRIO       22
#define TRACE_EVENT_STOP      0x01

#define TRACE_DETECT_BLACK 0

static float g_kp = 16.0f;
static float g_ki = 0.0f;
static float g_kd = 0.0f;
static int g_base_speed = TRACE_SPEED_FORWARD;

static float g_last_error = 0;
static float g_integral = 0;
static unsigned long long g_last_seen_tick = 0;
static float g_last_valid_error = 0;

static osal_task *g_trace_task = NULL;
static osal_event g_trace_event;
static bool g_event_inited = false;
static volatile bool g_trace_running = false;

/* PID 参数互斥锁：保护 set_pid 与 tick 的并发访问 */
static osal_mutex g_pid_mutex;
static bool g_pid_mutex_inited = false;

typedef struct {
  uint8_t left;
  uint8_t middle;
  uint8_t right;
  float error;
} TraceErrorMap;

static const TraceErrorMap g_trace_error_table[] = {
    {1, 1, 0, -1.0f}, {0, 1, 1, 1.0f},  {1, 0, 0, -2.0f},
    {0, 0, 1, 2.0f},  {0, 1, 0, 0.0f},  {1, 1, 1, 0.0f},
};

static float calculate_trace_error(unsigned int left, unsigned int middle,
                                   unsigned int right) {
  int table_size = sizeof(g_trace_error_table) / sizeof(g_trace_error_table[0]);
  for (int i = 0; i < table_size; i++) {
    if (g_trace_error_table[i].left == left &&
        g_trace_error_table[i].middle == middle &&
        g_trace_error_table[i].right == right) {
      return g_trace_error_table[i].error;
    }
  }
  return 0.0f;
}

static float calculate_pid(float error) {
  g_integral += error;
  if (g_integral > 50) g_integral = 50;
  if (g_integral < -50) g_integral = -50;
  if (error > 1 || error < -1) g_integral = 0;

  float p_term = g_kp * error;
  float i_term = g_ki * g_integral;
  float d_term = g_kd * (error - g_last_error);
  g_last_error = error;
  return p_term + i_term + d_term;
}

static void trace_sample_adc(void) {
  adc_scan_config_t config = {.type = 0, .freq = 1};
  uapi_adc_auto_scan_ch_enable(TCRT5000_LEFT_ADC_CHANNEL, config,
                               tcrt5000_adc_callback);
  uapi_adc_auto_scan_ch_disable(TCRT5000_LEFT_ADC_CHANNEL);
  uapi_adc_auto_scan_ch_enable(TCRT5000_MIDDLE_ADC_CHANNEL, config,
                               tcrt5000_adc_callback);
  uapi_adc_auto_scan_ch_disable(TCRT5000_MIDDLE_ADC_CHANNEL);
  uapi_adc_auto_scan_ch_enable(TCRT5000_RIGHT_ADC_CHANNEL, config,
                               tcrt5000_adc_callback);
  uapi_adc_auto_scan_ch_disable(TCRT5000_RIGHT_ADC_CHANNEL);
}

static void trace_tick_once(void) {
  trace_sample_adc();

  unsigned int left = tcrt5000_get_left();
  unsigned int middle = tcrt5000_get_middle();
  unsigned int right = tcrt5000_get_right();
  unsigned long long now = osal_get_jiffies();

  static int debug_cnt = 0;
  if (++debug_cnt >= 20) {
    debug_cnt = 0;
    printf("TRACE: L=%d M=%d R=%d, ADC: L=%d M=%d R=%d mV\n", left, middle,
           right, tcrt5000_get_left_adc(), tcrt5000_get_middle_adc(),
           tcrt5000_get_right_adc());
  }

  robot_mgr_update_ir_status(left, middle, right);
  float error = calculate_trace_error(left, middle, right);

  if (left == TRACE_DETECT_BLACK || middle == TRACE_DETECT_BLACK ||
      right == TRACE_DETECT_BLACK) {
    g_last_seen_tick = now;
    g_last_valid_error = error;

    float pid_output;
    int current_base_speed;
    if (g_pid_mutex_inited) osal_mutex_lock(&g_pid_mutex);
    pid_output = calculate_pid(error);
    current_base_speed = g_base_speed;
    if (g_pid_mutex_inited) osal_mutex_unlock(&g_pid_mutex);
    if (error >= 2 || error <= -2)
      current_base_speed = (int)(g_base_speed * 0.6f);
    else if (error >= 1 || error <= -1)
      current_base_speed = (int)(g_base_speed * 0.9f);

    int pid_out_int =
        (int)(pid_output > 0 ? (pid_output + 0.5f) : (pid_output - 0.5f));

    int left_speed = current_base_speed + pid_out_int;
    int right_speed = current_base_speed - pid_out_int;
    if (left_speed > 100) left_speed = 100;
    if (left_speed < -100) left_speed = -100;
    if (right_speed > 100) right_speed = 100;
    if (right_speed < -100) right_speed = -100;

    motor_executor_push_cmd((int8_t)left_speed, (int8_t)right_speed);
  } else {
    if (now - g_last_seen_tick < osal_msecs_to_jiffies(TRACE_LOST_TIMEOUT_MS)) {
      int search_speed = TRACE_SEARCH_SPEED;
      if (g_last_valid_error < -0.5f) {
        motor_executor_push_cmd(search_speed, -search_speed / 2);
      } else if (g_last_valid_error > 0.5f) {
        motor_executor_push_cmd(-search_speed / 2, search_speed);
      } else {
        motor_executor_push_cmd(search_speed, search_speed);
      }
    } else {
      motor_executor_push_cmd(0, 0);
    }
  }
}

static int trace_task_entry(void *arg) {
  (void)arg;
  printf("[Trace] 循迹任务启动\r\n");

  while (g_trace_running) {
    // 用事件超时实现 20ms 周期；exit 时写 STOP 事件立即唤醒
    int ret = osal_event_read(&g_trace_event, TRACE_EVENT_STOP, TRACE_TICK_MS,
                              OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
    if (ret > 0 && ((unsigned int)ret & TRACE_EVENT_STOP)) {
      break;
    }
    trace_tick_once();
  }

  motor_executor_push_cmd(0, 0);
  printf("[Trace] 循迹任务退出\r\n");
  g_trace_task = NULL;   /* 退出前自清句柄，供 exit 同步 */
  return 0;
}

void mode_trace_set_pid(int type, int value) {
  if (g_pid_mutex_inited) osal_mutex_lock(&g_pid_mutex);
  if (type == 1)
    g_kp = (float)value / 1000.0f;
  else if (type == 2)
    g_ki = (float)value / 10000.0f;
  else if (type == 3)
    g_kd = (float)value / 500.0f;
  else if (type == 4)
    g_base_speed = value;
  printf("PID Set: Kp=%.2f Ki=%.3f Kd=%.2f Speed=%d\r\n", g_kp, g_ki, g_kd,
         g_base_speed);
  g_integral = 0;
  g_last_error = 0;
  if (g_pid_mutex_inited) osal_mutex_unlock(&g_pid_mutex);
}

void mode_trace_save_pid(void) {
  if (g_pid_mutex_inited) osal_mutex_lock(&g_pid_mutex);
  float kp = g_kp, ki = g_ki, kd = g_kd;
  int16_t speed = (int16_t)g_base_speed;
  if (g_pid_mutex_inited) osal_mutex_unlock(&g_pid_mutex);

  errcode_t ret = storage_service_save_pid_params(kp, ki, kd, speed);
  printf("PID Save: Kp=%.2f Ki=%.3f Kd=%.2f Speed=%d 结果=%d\r\n",
         kp, ki, kd, speed, ret);
}

void mode_trace_enter(void) {
  printf("进入循迹模式...\r\n");

  g_last_seen_tick = osal_get_jiffies();
  g_last_error = 0;
  g_integral = 0;
  g_last_valid_error = 0;

  float kp, ki, kd;
  int16_t speed;
  storage_service_get_pid_params(&kp, &ki, &kd, &speed);
  g_kp = kp;
  g_ki = ki;
  g_kd = kd;
  g_base_speed = speed;

  if (!g_pid_mutex_inited) {
    if (osal_mutex_init(&g_pid_mutex) == OSAL_SUCCESS) {
      g_pid_mutex_inited = true;
    }
  }

  if (!g_event_inited) {
    if (osal_event_init(&g_trace_event) == OSAL_SUCCESS) {
      g_event_inited = true;
    } else {
      printf("[Trace] 事件初始化失败\r\n");
      return;
    }
  }

  if (g_trace_task != NULL) return;

  g_trace_running = true;
  osal_kthread_lock();
  g_trace_task = osal_kthread_create((osal_kthread_handler)trace_task_entry,
                                      NULL, "trace_task", TRACE_TASK_STACK_SIZE);
  if (g_trace_task != NULL) {
    osal_kthread_set_priority(g_trace_task, TRACE_TASK_PRIO);
  }
  osal_kthread_unlock();
}

void mode_trace_exit(void) {
  if (g_trace_task != NULL) {
    g_trace_running = false;
    if (g_event_inited) {
      osal_event_write(&g_trace_event, TRACE_EVENT_STOP);
    }
    /* 等待任务自行退出（最多 200ms），避免 enter 时跳过创建 */
    int wait = 0;
    while (g_trace_task != NULL && wait < 20) {
      osal_msleep(10);
      wait++;
    }
    g_trace_task = NULL;  /* 兜底 */
  }
  motor_executor_push_cmd(0, 0);
}
