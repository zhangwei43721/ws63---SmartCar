#ifndef MODE_MGR_H
#define MODE_MGR_H

#include "../car_common.h"

// 模式状态机：全车模式的唯一拥有者与转移执行者。
// 各通道/按键通过 mode_mgr_post() 投递切换意图，状态机在专用任务上下文中
// 串行执行 enter/exit，避免在通道回调里直接切换模式造成的竞态。
void mode_mgr_init(void);                               // 创建模式切换命令队列（通道注册前调用）
void mode_mgr_run(void);                                // 状态机主循环（阻塞，不返回）
bool mode_mgr_post(CarStatus status, uint32_t source);  // 投递模式切换命令（可在中断中调用）
CarStatus mode_mgr_current(void);                       // 当前模式
const char *mode_mgr_name(CarStatus status);            // 模式枚举转可读字符串

#endif
