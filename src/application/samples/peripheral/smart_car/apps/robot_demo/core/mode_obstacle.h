#ifndef MODE_OBSTACLE_H
#define MODE_OBSTACLE_H

void mode_obstacle_enter(void); /* 进入避障模式（创建任务 + 启动超声波） */
void mode_obstacle_exit(void);  /* 退出避障模式（通知任务停止 + 等待退出） */

#endif
