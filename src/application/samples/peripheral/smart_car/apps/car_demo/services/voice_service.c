/**
 * @file voice_service.c
 * @brief 语音模块命令服务实现 - 消息队列驱动 + 看门狗任务
 *
 * 设计：
 *  - UART 回调（运行在 BSP UART 上下文）只做一件事：把字节投到消息队列；
 *    业务逻辑（电机控制 / 模式切换）一律不在回调里跑
 *  - voice_task：阻塞读消息队列；收到字节后 process_command；
 *    同时承担命令超时看门狗（用 read_copy 的 timeout 实现"剩余时间等待"）
 */

#include "voice_service.h"

#include <stdio.h>

#include "../../../drivers/uart/bsp_uart.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../car_common.h"
#include "soc_osal.h"

#define VOICE_CMD_TIMEOUT_MS 1000 // 语音命令超时时间(ms)
#define MOTOR_SPEED_HIGH 100      // 高速电机速度
#define MOTOR_SPEED_TURN 50       // 转弯电机速度
#define TURN_DURATION_MS 400      // 转弯持续时间(ms)


#define VOICE_RX_QUEUE_DEPTH 32 // UART 接收队列深度

static osal_task *g_voice_task = NULL; // 语音命令处理任务句柄
static unsigned long g_rx_queue = 0;   // UART 接收消息队列 ID
static bool g_rx_queue_inited = false; // 接收队列是否已初始化

// 命令超时停车看门狗：用 osal_timer 单次定时，避免 32-bit RISC-V 上做软件模拟 64-bit jiffies 减法
static osal_timer g_voice_stop_timer;
static bool g_voice_stop_timer_inited = false;   // 停车定时器是否已初始化
static volatile bool g_voice_cmd_active = false; // 单写者(voice_task)，定时器回调清除

// 语音命令超时回调，自动停车
static void voice_stop_timer_cb(unsigned long arg)
{
    (void)arg;
    g_voice_cmd_active = false;
    bsp_motor_push_cmd(0, 0);
}

// 设置电机运动并启动/重置超时定时器
static void voice_set_motion(int8_t l, int8_t r, uint32_t ms)
{
    car_mgr_manual_drive(l, r, MODE_SRC_VOICE);
    if (g_voice_stop_timer_inited) {
        osal_timer_stop(&g_voice_stop_timer);
    }
    if (ms == 0 || (l == 0 && r == 0)) {
        g_voice_cmd_active = false;
    } else if (g_voice_stop_timer_inited) {
        // 必须用 osal_timer_mod 重建为 NO_SELFDELETE 单次模式；单改 interval + start 在 PERIOD 模式下无效
        osal_timer_mod(&g_voice_stop_timer, ms);
        g_voice_cmd_active = true;
    }
}

// 运动命令表：直接驱动 voice_set_motion，避免 switch/case 与魔术数字
typedef struct {
    uint8_t code;
    int8_t l;
    int8_t r;
    uint32_t ms;
} voice_motion_entry_t;

static const voice_motion_entry_t g_motion_table[] = {
    {VOICE_CMD_STOP, 0, 0, 0},
    {VOICE_CMD_FORWARD, MOTOR_SPEED_HIGH, MOTOR_SPEED_HIGH, VOICE_CMD_TIMEOUT_MS},
    {VOICE_CMD_BACKWARD, -MOTOR_SPEED_HIGH, -MOTOR_SPEED_HIGH, VOICE_CMD_TIMEOUT_MS},
    {VOICE_CMD_LEFT, -MOTOR_SPEED_TURN, MOTOR_SPEED_TURN, TURN_DURATION_MS},
    {VOICE_CMD_RIGHT, MOTOR_SPEED_TURN, -MOTOR_SPEED_TURN, TURN_DURATION_MS},
};

// 模式切换表：cmd 0x10..0x13 对应索引
static const CarStatus g_mode_table[] = {
    CAR_STOP_STATUS,
    CAR_TRACE_STATUS,
    CAR_OBSTACLE_AVOIDANCE_STATUS,
    CAR_WIFI_CONTROL_STATUS,
};
#define VOICE_MODE_CMD_BASE 0x10                                              // 模式切换命令起始值
#define VOICE_MODE_CMD_COUNT (sizeof(g_mode_table) / sizeof(g_mode_table[0])) // 模式表条目数

// 解析语音命令，执行运动或模式切换
static void process_command(uint8_t cmd)
{
    if (cmd >= VOICE_MODE_CMD_BASE) {
        unsigned idx = cmd - VOICE_MODE_CMD_BASE;
        if (idx < VOICE_MODE_CMD_COUNT) {
            voice_set_motion(0, 0, 0);
            car_mgr_post_mode(g_mode_table[idx], MODE_SRC_VOICE);
        }
        return;
    }

    for (unsigned i = 0; i < sizeof(g_motion_table) / sizeof(g_motion_table[0]); i++) {
        if (g_motion_table[i].code == cmd) {
            voice_set_motion(g_motion_table[i].l, g_motion_table[i].r, g_motion_table[i].ms);
            return;
        }
    }
}

// UART 回调：仅投消息队列，不做任何业务
static void voice_rx_callback(const uint8_t *data, uint16_t length)
{
    if (!data || length == 0 || !g_rx_queue_inited)
        return;
    for (uint16_t i = 0; i < length; i++) {
        uint8_t b = data[i];
        (void)osal_msgq_overwrite(g_rx_queue, VOICE_RX_QUEUE_DEPTH, &b, sizeof(b));
    }
}

// voice_task：消费 RX 字节；命令超时停车交给 osal_timer 处理，任务只阻塞读队列
static int voice_main_task(void *arg)
{
    (void)arg;

    while (1) {
        uint8_t byte;
        unsigned int sz = sizeof(byte);
        int ret = osal_msg_queue_read_copy(g_rx_queue, &byte, &sz, OSAL_WAIT_FOREVER);
        if (ret == OSAL_SUCCESS && sz == sizeof(byte)) {
            process_command(byte);
        }
    }
    return 0;
}

// 初始化语音服务：创建定时器、消息队列、UART和语音任务
void voice_service_init(void)
{
    g_voice_cmd_active = false;

    if (!g_voice_stop_timer_inited) {
        g_voice_stop_timer.interval = VOICE_CMD_TIMEOUT_MS;
        g_voice_stop_timer.handler = voice_stop_timer_cb;
        g_voice_stop_timer.data = 0;
        if (osal_timer_init(&g_voice_stop_timer) == OSAL_SUCCESS) {
            g_voice_stop_timer_inited = true;
        } else {
            printf("[语音] 定时器初始化失败\r\n");
        }
    }

    if (!g_rx_queue_inited) {
        if (osal_msg_queue_create("voice_rx", VOICE_RX_QUEUE_DEPTH, &g_rx_queue, 0, sizeof(uint8_t)) == OSAL_SUCCESS) {
            g_rx_queue_inited = true;
        } else {
            printf("[语音] 队列初始化失败\r\n");
            return;
        }
    }

    if (bsp_uart_init(voice_rx_callback) != 0) {
        printf("[语音] UART 初始化失败！\r\n");
        return;
    }

    g_voice_task = car_task_create_locked("voice_task", (osal_kthread_handler)voice_main_task, NULL,
                                            2048, 29);

    printf("[语音] 服务已启动\r\n");
}
