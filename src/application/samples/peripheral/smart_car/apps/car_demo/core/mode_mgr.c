/**
 * @file mode_mgr.c
 * @brief 模式状态机实现：切换命令队列 + enter/exit 生命周期调度（纯事件驱动）
 */

#include "mode_mgr.h"

#include <stdio.h>

#include "car_state.h"
#include "mode_obstacle.h"
#include "mode_trace.h"
#include "../services/ui_service.h"
#include "soc_osal.h"

static volatile CarStatus g_status = CAR_STOP_STATUS; // 当前小车运行模式（按键 ISR 读 vs 状态机任务写）

#define MODE_CMD_QUEUE_DEPTH 4           // 模式切换消息队列深度
static unsigned long g_mode_queue = 0;   // 模式切换命令队列ID
static bool g_mode_queue_inited = false; // 模式队列是否已初始化

static const char *const k_mode_names[] = {
    "停止",
    "循迹",
    "避障",
    "遥控",
};

// 将CarStatus枚举值转换为中文模式名称字符串
const char *mode_mgr_name(CarStatus status)
{
    unsigned idx = (unsigned)status;
    if (idx < sizeof(k_mode_names) / sizeof(k_mode_names[0]))
        return k_mode_names[idx];
    return "?";
}

// 创建模式切换命令队列（仅执行一次）
void mode_mgr_init(void)
{
    if (g_mode_queue_inited)
        return;
    if (osal_msg_queue_create("mode_q", MODE_CMD_QUEUE_DEPTH, &g_mode_queue, 0, sizeof(ModeCmdMsg)) == OSAL_SUCCESS)
        g_mode_queue_inited = true;
    else
        printf("ModeMgr: 模式队列创建失败\r\n");
}

// 向模式切换消息队列投递切换请求（可在中断中调用）
bool mode_mgr_post(CarStatus status, uint32_t source)
{
    if (!g_mode_queue_inited)
        return false;

    ModeCmdMsg msg = {.status = status, .source = source};

    // osal_msgq_overwrite 内部已关中断保护，此处无需再包一层
    int ret = osal_msgq_overwrite(g_mode_queue, MODE_CMD_QUEUE_DEPTH, &msg, sizeof(msg));
    return (ret == OSAL_SUCCESS);
}

// 当前模式
CarStatus mode_mgr_current(void)
{
    return g_status;
}

// 应用新模式：更新全局状态、刷新OLED显示。返回 true 表示状态发生了变化
static bool mode_mgr_apply(CarStatus status)
{
    if (g_status == status)
        return false;

    printf("模式切换：%s -> %s\r\n", mode_mgr_name(g_status), mode_mgr_name(status));

    g_status = status;
    car_state_set_mode(status);

    ui_show_mode_page(status);
    return true;
}

/**
 * @brief 状态机主循环 —— 纯事件驱动
 * @note 阻塞在模式切换消息队列上，无消息时永久休眠让出 CPU。
 *       只在收到模式切换请求时才执行 enter/exit 状态转移。
 */
void mode_mgr_run(void)
{
    while (1) {
        ModeCmdMsg msg;
        unsigned int sz = sizeof(msg);

        // 阻塞等待模式切换消息
        int ret = osal_msg_queue_read_copy(g_mode_queue, &msg, &sz, OSAL_WAIT_FOREVER);
        if (ret != OSAL_SUCCESS)
            continue;

        // 排空队列中剩余消息，只保留最后一条意图，避免对中间状态做无意义的 enter/exit
        ModeCmdMsg drain;
        unsigned int dsz = sizeof(drain);
        while (osal_msg_queue_read_copy(g_mode_queue, &drain, &dsz, OSAL_MSGQ_NO_WAIT) == OSAL_SUCCESS) {
            msg = drain;
            dsz = sizeof(drain);
        }

        // 只对最终意图 apply 一次，状态未变则跳过
        if (!mode_mgr_apply(msg.status))
            continue;

        // 执行状态转移。exit 返回 false 表示旧任务未及时退出（句柄已保留防重复创建），
        // 此时放弃 enter 并保持安全，避免旧任务仍跑时再叠一个新任务抢电机。
        switch (g_status) {
            case CAR_TRACE_STATUS:
                if (mode_obstacle_exit()) {
                    mode_trace_enter();
                } else {
                    printf("[ModeMgr] 避障退出失败，放弃进入循迹\r\n");
                }
                break;
            case CAR_OBSTACLE_AVOIDANCE_STATUS:
                if (mode_trace_exit()) {
                    mode_obstacle_enter();
                } else {
                    printf("[ModeMgr] 循迹退出失败，放弃进入避障\r\n");
                }
                break;
            default: {
                bool t = mode_trace_exit();
                bool o = mode_obstacle_exit();
                if (!t || !o) {
                    printf("[ModeMgr] 模式退出异常 (trace=%d, obstacle=%d)\r\n", (int)t, (int)o);
                }
                break;
            }
        }
    }
}
