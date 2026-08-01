/**
 * @file car_state.c
 * @brief 状态仓库实现：CarState 的互斥保护读写
 */

#include "car_state.h"

#include <stdio.h>

static CarState g_car_state = {0};              // 全局机器人状态
static osal_mutex g_state_mutex;                // 保护 g_car_state 的互斥锁
static bool g_state_mutex_inited = false;       // 状态互斥锁是否已初始化

// 初始化状态仓库互斥锁（仅执行一次，init 失败属于 BUG：后续访问会打印告警）
void car_state_init(void)
{
    if (g_state_mutex_inited)
        return;
    if (osal_mutex_init(&g_state_mutex) == OSAL_SUCCESS)
        g_state_mutex_inited = true;
    else
        printf("CarState: 状态互斥锁初始化失败\r\n");
}

// 线程安全地获取当前 CarState 快照
void car_state_get_copy(CarState *out)
{
    if (out == NULL)
        return;
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    *out = g_car_state;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

// 写入当前模式（仅 mode_mgr 在状态转移时调用）
void car_state_set_mode(CarStatus mode)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.mode = mode;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

// 更新全局状态中的超声波测距值
void car_state_update_distance(float distance)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.distance = distance;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

// 更新全局状态中的三路循迹红外传感器状态
void car_state_update_ir_status(unsigned int left, unsigned int middle, unsigned int right)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.ir_left = left;
    g_car_state.ir_middle = middle;
    g_car_state.ir_right = right;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

// 更新原始采样 ADC
void car_state_update_adc_values(uint32_t left, uint32_t middle, uint32_t right)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.adc_left = left;
    g_car_state.adc_middle = middle;
    g_car_state.adc_right = right;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

// 更新当前活跃阈值
void car_state_update_thresholds(uint16_t left, uint16_t middle, uint16_t right)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.th_left = left;
    g_car_state.th_middle = middle;
    g_car_state.th_right = right;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}
