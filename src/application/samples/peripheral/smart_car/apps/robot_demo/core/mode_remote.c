#include "mode_remote.h"

#include <stdio.h>

#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../services/voice_service.h"
#include "../services/udp_service.h"
#include "robot_config.h"
#include "soc_osal.h"

#define TIMEOUT_MS 500  // 信号丢失保护时间

static unsigned long long g_last_tick = 0;  // 上次收到命令的时间

void mode_remote_enter(void) {
  printf("Robot: 遥控模式\r\n");
  l9110s_set_differential(0, 0);  // 先停车
  g_last_tick = osal_get_jiffies();
}

void mode_remote_tick(void) {
  int8_t m1 = 0, m2 = 0;
  bool has_new_cmd = false;

  // 1. 先看串口有没有命令
  if (voice_service_is_cmd_active()) {
    voice_service_get_motor_cmd(&m1, &m2);
    has_new_cmd = true;
  } else {
    // 2. 再看 WiFi 有没有命令
    // 把缓冲区里的旧数据全部扔掉，只保留最后一次的 m1,m2
    while (udp_service_pop_cmd(&m1, &m2)) has_new_cmd = true;
  }

  if (has_new_cmd) {  // 1: 收到新指令 -> 刷新时间，执行动作
    g_last_tick = osal_get_jiffies();
    l9110s_set_differential(m1, m2);
  } else {  // 2: 没有新指令 -> 检查是不是断联了
    unsigned long long now = osal_get_jiffies();
    if ((now - g_last_tick) > osal_msecs_to_jiffies(TIMEOUT_MS))
      l9110s_set_differential(0, 0);
  }
}

void mode_remote_exit(void) { l9110s_set_differential(0, 0); }