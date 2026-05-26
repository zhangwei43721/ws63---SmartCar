/**
 ****************************************************************************************************
 * @file        robot_demo.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-16
 * @brief       LiteOS 智能小车主入口 + 状态机调度
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 */

#include "app_init.h"
#include "common_def.h"
#include "core/mode_obstacle.h"
#include "core/mode_trace.h"
#include "robot_common.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "osal_timer.h"
#include "pinctrl.h"
#include "securec.h"
#include "services/captive_portal_service.h"
#include "services/ota_service.h"
#include "services/sle_service.h"
#include "../../platform/storage_service.h"
#include "services/ui_service.h"
#include "services/udp_service.h"
#include "services/voice_service.h"
#include "soc_osal.h"
#include "watchdog.h"

#include "../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../drivers/l9110s/bsp_l9110s.h"
#include "../../drivers/motor_control/bsp_motor.h"
#include "../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "services/wifi_mgr_service.h"

/* ============================================================
 * 按键中断
 * ============================================================ */

#define ROBOT_MODE_SWITCH_GPIO 3
static unsigned long long button_time_tick = 0;

static void mode_switch_isr(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);

    unsigned long long current_tick = osal_get_jiffies();
    if ((current_tick - button_time_tick) < osal_msecs_to_jiffies(200)) {
        return;
    }
    button_time_tick = current_tick;

    CarStatus next_status = (CarStatus)((robot_mgr_get_status() + 1) % 4);
    robot_mgr_post_mode(next_status, MODE_SRC_BUTTON);
}

static void robot_key_init(void)
{
    uapi_pin_set_mode(ROBOT_MODE_SWITCH_GPIO, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(ROBOT_MODE_SWITCH_GPIO, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(ROBOT_MODE_SWITCH_GPIO, PIN_PULL_TYPE_UP);

    uapi_gpio_register_isr_func(ROBOT_MODE_SWITCH_GPIO, GPIO_INTERRUPT_FALLING_EDGE, mode_switch_isr);
}

/* ============================================================
 * 状态机核心
 * ============================================================ */

static CarStatus g_status = CAR_STOP_STATUS;
static CarStatus g_last_status = CAR_STOP_STATUS;

#define MODE_CMD_QUEUE_DEPTH 4
static unsigned long g_mode_queue = 0;
static bool g_mode_queue_inited = false;

static RobotState g_robot_state = {0};
static osal_mutex g_state_mutex;
static bool g_state_mutex_inited = false;

static void robot_mgr_state_mutex_init(void)
{
    if (g_state_mutex_inited)
        return;
    if (osal_mutex_init(&g_state_mutex) == OSAL_SUCCESS) {
        g_state_mutex_inited = true;
    } else {
        printf("RobotMgr: 状态互斥锁初始化失败\r\n");
    }
}

static void robot_mgr_apply_status(CarStatus status)
{
    if (g_status == status)
        return;

    printf("模式切换：%s -> %s\r\n", robot_mode_name(g_status), robot_mode_name(status));

    g_status = status;
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_robot_state.mode = status;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);

    ui_show_mode_page(status);
}

static void robot_mgr_do_exit(CarStatus status)
{
    switch (status) {
        case CAR_TRACE_STATUS:
            mode_trace_exit();
            break;
        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            mode_obstacle_exit();
            break;
        default:
            break;
    }
}

static void robot_mgr_do_enter(CarStatus status)
{
    switch (status) {
        case CAR_TRACE_STATUS:
            mode_trace_enter();
            break;
        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            mode_obstacle_enter();
            break;
        default:
            break;
    }
}

/* ============================================================
 * 公共接口
 * ============================================================ */

CarStatus robot_mgr_get_status(void)
{
    return g_status;
}

bool robot_mgr_post_mode(CarStatus status, uint32_t source)
{
    if (!g_mode_queue_inited)
        return false;

    ModeCmdMsg msg = {.status = status, .source = source};

    uint32_t irq_sts = osal_irq_lock();
    int ret = osal_msgq_overwrite(g_mode_queue, MODE_CMD_QUEUE_DEPTH, &msg, sizeof(msg));
    osal_irq_restore(irq_sts);

    return (ret == OSAL_SUCCESS);
}

void robot_mgr_get_state_copy(RobotState *out)
{
    if (out == NULL)
        return;
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    *out = g_robot_state;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

void robot_mgr_update_distance(float distance)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_robot_state.distance = distance;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

void robot_mgr_update_ir_status(unsigned int left, unsigned int middle, unsigned int right)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_robot_state.ir_left = left;
    g_robot_state.ir_middle = middle;
    g_robot_state.ir_right = right;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

/* ============================================================
 * 统一初始化与任务入口
 * ============================================================ */

static void robot_system_init(void)
{
    storage_service_init();

    // 驱动初始化（先于一切任务创建）
    l9110s_init();
    hcsr04_init();
    tcrt5000_adc_init();

    // 关键控制任务先创建，确保任务池不会因后续网络服务耗尽而抢占
    robot_mgr_state_mutex_init();
    bsp_motor_init();
    
    // WiFi 管理任务
    bsp_wifi_mgr_init();
    bsp_wifi_msg_t wifi_start = {.id = WIFI_MSG_START};
    bsp_wifi_mgr_send_msg(&wifi_start);

    // 其它服务（UI / UDP / OTA / SLE / Portal / Voice）
    ui_service_init();
    udp_service_init();
    ota_service_init();
    sle_service_init();
    captive_portal_service_init();
    voice_service_init();

    robot_key_init();
    if (!g_mode_queue_inited) {
        if (osal_msg_queue_create("mode_q", MODE_CMD_QUEUE_DEPTH, &g_mode_queue, 0, sizeof(ModeCmdMsg)) == OSAL_SUCCESS)
            g_mode_queue_inited = true;
        else
            printf("RobotMgr: 模式队列创建失败\r\n");
    }

    robot_mgr_apply_status(CAR_STOP_STATUS);
    g_last_status = CAR_STOP_STATUS;

    printf("Robot: 系统初始化完成\r\n");
    printf("[FIRMWARE] OTA_TEST_BUILD_20250519_V2\r\n");
}

#define ROBOT_TASK_STACK_SIZE (1024 * 4)
#define ROBOT_TASK_PRIO 25

/**
 * @brief 主状态机任务 —— 纯事件驱动
 * @note 阻塞在模式切换消息队列上，无消息时永久休眠让出 CPU。
 *       只在收到模式切换请求时才执行 enter/exit 状态转移。
 */
static int robot_main_task(void *arg)
{
    UNUSED(arg);

    robot_system_init();

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

        // 只对最终意图 apply 一次
        robot_mgr_apply_status(msg.status);

        // 执行状态转移
        if (g_status != g_last_status) {
            robot_mgr_do_exit(g_last_status);
            robot_mgr_do_enter(g_status);
            g_last_status = g_status;
        }
    }

    return 0;
}

static void robot_demo_entry(void)
{
    (void)robot_task_create_locked("robot_main_task", (osal_kthread_handler)robot_main_task, NULL,
                                   ROBOT_TASK_STACK_SIZE, ROBOT_TASK_PRIO);
    printf("智能小车主任务已创建\r\n");
}

app_run(robot_demo_entry);