#ifndef CAR_STATE_H
#define CAR_STATE_H

#include "../car_common.h"

// 状态仓库：CarState 的互斥保护读写。全车实时状态的唯一拥有者，
// UI / Web / UDP 遥测等只能通过这里取快照，禁止各自另存副本。
void car_state_init(void);                      // 初始化状态仓库互斥锁（最先调用）
void car_state_get_copy(CarState *out);         // 线程安全地获取 CarState 快照
void car_state_set_mode(CarStatus mode);        // 写入当前模式（仅 mode_mgr 调用）
void car_state_update_distance(float distance); // 更新超声波距离值（避障模式写入）
void car_state_update_ir_status(unsigned int left, unsigned int middle,
                                unsigned int right);                              // 更新三路红外传感器状态
void car_state_update_adc_values(uint32_t left, uint32_t middle, uint32_t right); // 更新原始采样 ADC
void car_state_update_thresholds(uint16_t left, uint16_t middle, uint16_t right); // 更新当前活跃阈值

#endif
