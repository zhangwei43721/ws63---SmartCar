#include "mode_obstacle.h"

#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "robot_config.h"
#include "robot_mgr.h"

/* ================= 参数配置 ================= */
#define OBSTACLE_LIMIT 20.0f  // 障碍物判定距离 (cm)
#define TIME_BACK_MS 300      // 后退一下的时间 (防止转弯蹭墙)
#define TIME_TURN_90_MS 650   // 原地转90度所需时间 (根据车速调整)
#define TIME_WAIT_STABLE 300  // 每次转完停顿检测的时间

/**
 * @brief 避障模式进入
 */
void mode_obstacle_enter(void) {
  printf("进入智能避障模式\r\n");
  CAR_STOP();
}

/**
 * @brief 避障模式周期回调
 */
void mode_obstacle_tick(void) {
  // 1. 获取当前前方距离
  float current_dist = hcsr04_get_distance();
  robot_mgr_update_distance(current_dist);

  // 情况 A: 前方开阔，直接走
  if (current_dist > OBSTACLE_LIMIT) {
    CAR_FORWARD();
    return;
  }

  // 情况 B: 前方受阻，进入"尝试突围"流程
  printf("前方受阻(%.1fcm)，开始尝试寻找出口...\r\n", current_dist);

  CAR_STOP();  // 先停车
  osal_msleep(100);
  int attempts = 0;  // 尝试计数器
  // 循环尝试最多 4 次 (4 * 90度 = 360度)
  while (attempts < 4) {
    printf("尝试第 %d 次...\r\n", attempts + 1);
    CAR_BACKWARD();  // 1. 稍微后退
    osal_msleep(TIME_BACK_MS);
    CAR_STOP();
    osal_msleep(100);
    CAR_LEFT();  // 2. 左转 90 度
    osal_msleep(TIME_TURN_90_MS);
    CAR_STOP();  // 3. 停车稳住
    osal_msleep(TIME_WAIT_STABLE);

    float new_dist = hcsr04_get_distance();
    printf("转向后距离: %.1f\r\n", new_dist);

    if (new_dist > OBSTACLE_LIMIT) {
      printf("找到出口！继续前进。\r\n");
      CAR_FORWARD();
      return;
    }
    attempts++;  // 如果还是不通，计数器加1，继续循环
  }

  printf("四面被围，彻底停车待援！\r\n");
  CAR_STOP();
  while (1) {
    osal_msleep(200);
  }
}

/**
 * @brief 避障模式退出
 */
void mode_obstacle_exit(void) { CAR_STOP(); }