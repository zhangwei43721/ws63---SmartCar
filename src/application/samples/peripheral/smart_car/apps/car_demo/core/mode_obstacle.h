#ifndef MODE_OBSTACLE_H
#define MODE_OBSTACLE_H

#include <stdbool.h>

void mode_obstacle_enter(void); // 进入避障模式（创建任务 + 启动超声波）
bool mode_obstacle_exit(void);  // 退出避障模式（通知任务停止 + 等待退出）；false=任务未及时退出

#endif
