/**
 * @file voice_service.c
 * @brief 语音模块命令服务实现 - 事件驱动 + 看门狗任务
 *
 * 设计：
 *  - UART 回调：解析命令后直接推 Motor 队列，并写事件唤醒看门狗
 *  - 看门狗任务：阻塞在事件上；收到事件后按 expire 时间等待，
 *    超时未续命则推一条 0,0 停车命令
 *  - 不再有 voice_service_tick 轮询
 */

#include "voice_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/uart/bsp_uart.h"
#include "../core/motor_executor.h"
#include "../robot_common.h"
#include "soc_osal.h"

#define VOICE_CMD_TIMEOUT_MS 1000
#define MOTOR_SPEED_HIGH 100
#define MOTOR_SPEED_TURN 50
#define TURN_DURATION_MS 400

#define VOICE_TASK_STACK_SIZE 2048
#define VOICE_TASK_PRIO 29
#define VOICE_EVENT_NEW_CMD 0x01

static osal_event g_voice_event;
static bool g_event_inited = false;
static osal_task *g_voice_task = NULL;

// 共享：当前命令的到期时间（jiffies），0 表示无活动命令
static volatile unsigned long long g_expire_tick = 0;

static void voice_set_motion(int8_t l, int8_t r, uint32_t ms)
{
    motor_executor_push_cmd(l, r);
    if (ms == 0 || (l == 0 && r == 0)) {
        g_expire_tick = 0;
    } else {
        g_expire_tick = osal_get_jiffies() + osal_msecs_to_jiffies(ms);
        if (g_event_inited) {
            osal_event_write(&g_voice_event, VOICE_EVENT_NEW_CMD);
        }
    }
}

static void process_command(uint8_t cmd)
{
    // 1. 模式切换 (0x10-0x1F)
    if (cmd >= 0x10) {
        static const CarStatus modes[] = {CAR_STOP_STATUS, CAR_TRACE_STATUS, CAR_OBSTACLE_AVOIDANCE_STATUS,
                                          CAR_WIFI_CONTROL_STATUS};
        if (cmd - 0x10 < 4) {
            voice_set_motion(0, 0, 0);
            robot_mgr_post_mode(modes[cmd - 0x10], MODE_SRC_VOICE);
        }
        return;
    }

    // 2. 运动控制：直接推电机命令，不切换模式
    switch (cmd) {
        case VOICE_CMD_STOP:
            voice_set_motion(0, 0, 0);
            break;
        case VOICE_CMD_FORWARD:
            voice_set_motion(MOTOR_SPEED_HIGH, MOTOR_SPEED_HIGH, VOICE_CMD_TIMEOUT_MS);
            break;
        case VOICE_CMD_BACKWARD:
            voice_set_motion(-MOTOR_SPEED_HIGH, -MOTOR_SPEED_HIGH, VOICE_CMD_TIMEOUT_MS);
            break;
        case VOICE_CMD_LEFT:
            voice_set_motion(-MOTOR_SPEED_TURN, MOTOR_SPEED_TURN, TURN_DURATION_MS);
            break;
        case VOICE_CMD_RIGHT:
            voice_set_motion(MOTOR_SPEED_TURN, -MOTOR_SPEED_TURN, TURN_DURATION_MS);
            break;
    }
}

static void voice_rx_callback(const uint8_t *data, uint16_t length)
{
    if (!data || length == 0)
        return;
    for (uint16_t i = 0; i < length; i++) {
        process_command(data[i]);
    }
}

// 看门狗任务：等到 expire 后推停车命令
static int voice_watchdog_task(void *arg)
{
    (void)arg;
    unsigned long long jiffies_per_sec = osal_msecs_to_jiffies(1000);
    if (jiffies_per_sec == 0)
        jiffies_per_sec = 1000;

    while (1) {
        // 无活动命令时无限期阻塞等待
        (void)osal_event_read(&g_voice_event, VOICE_EVENT_NEW_CMD, OSAL_EVENT_FOREVER,
                              OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);

        // 命令活跃期间，按剩余时间等待；中途收到新命令会重新设置 expire
        while (g_expire_tick != 0) {
            unsigned long long now = osal_get_jiffies();
            unsigned long long expire = g_expire_tick;
            if (now >= expire) {
                g_expire_tick = 0;
                motor_executor_push_cmd(0, 0);
                break;
            }
            unsigned long long remain_jiffies = expire - now;
            unsigned int remain_ms = (unsigned int)(remain_jiffies * 1000ULL / jiffies_per_sec);
            if (remain_ms == 0)
                remain_ms = 1;
            (void)osal_event_read(&g_voice_event, VOICE_EVENT_NEW_CMD, remain_ms, OSAL_WAITMODE_OR | OSAL_WAITMODE_CLR);
        }
    }
    return 0;
}

void voice_service_init(void)
{
    g_expire_tick = 0;
    if (osal_event_init(&g_voice_event) == OSAL_SUCCESS) {
        g_event_inited = true;
    } else {
        printf("[语音] 事件初始化失败\r\n");
        return;
    }

    if (bsp_uart_init(voice_rx_callback) != 0) {
        printf("[语音] UART 初始化失败！\r\n");
        return;
    }

    osal_kthread_lock();
    g_voice_task =
        osal_kthread_create((osal_kthread_handler)voice_watchdog_task, NULL, "voice_wd", VOICE_TASK_STACK_SIZE);
    if (g_voice_task != NULL) {
        osal_kthread_set_priority(g_voice_task, VOICE_TASK_PRIO);
    }
    osal_kthread_unlock();

    printf("[语音] 服务已启动\r\n");
}

bool voice_service_is_cmd_active(void)
{
    return g_expire_tick != 0;
}
