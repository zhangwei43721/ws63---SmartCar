/**
 ****************************************************************************************************
 * @file        car_demo.c
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
#include "car_common.h"
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
 * 状态机核心
 * ============================================================ */

static CarStatus g_status = CAR_STOP_STATUS; // 当前小车运行模式

#define MODE_CMD_QUEUE_DEPTH 4           // 模式切换消息队列深度
static unsigned long g_mode_queue = 0;   // 模式切换命令队列ID
static bool g_mode_queue_inited = false; // 模式队列是否已初始化

static CarState g_car_state = {0};    // 全局机器人状态（WiFi连接、电机等）
static osal_mutex g_state_mutex;          // 保护g_car_state的互斥锁
static bool g_state_mutex_inited = false; // 状态互斥锁是否已初始化

/* 初始化全局状态互斥锁（仅执行一次） */
static void car_mgr_state_mutex_init(void)
{
    if (g_state_mutex_inited)
        return;
    if (osal_mutex_init(&g_state_mutex) == OSAL_SUCCESS)
        g_state_mutex_inited = true;
    else
        printf("CarMgr: 状态互斥锁初始化失败\r\n");
}

/* 应用新模式：更新全局状态、刷新OLED显示。返回 true 表示状态发生了变化 */
static bool car_mgr_apply_status(CarStatus status)
{
    if (g_status == status)
        return false;

    printf("模式切换：%s -> %s\r\n", car_mode_name(g_status), car_mode_name(status));

    g_status = status;
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.mode = status;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);

    ui_show_mode_page(status);
    return true;
}

/* ============================================================
 * 按键中断
 * ============================================================ */

static unsigned long long button_time_tick = 0;

/* 按键中断回调：200ms消抖后循环切换小车模式 */
static void mode_switch_isr(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);

    unsigned long long current_tick = osal_get_jiffies();
    if ((current_tick - button_time_tick) < osal_msecs_to_jiffies(200))
        return;

    button_time_tick = current_tick;

    CarStatus next_status = (CarStatus)((g_status + 1) % 4);
    car_mgr_post_mode(next_status, MODE_SRC_BUTTON);
}

/* 初始化模式切换按键GPIO及下降沿中断 */
static void car_key_init(void)
{
    uapi_pin_set_mode(3, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(3, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(3, PIN_PULL_TYPE_UP);

    uapi_gpio_register_isr_func(3, GPIO_INTERRUPT_FALLING_EDGE, mode_switch_isr);
}

/* ============================================================
 * 公共接口
 * ============================================================ */

/* 向模式切换消息队列投递切换请求（可在中断中调用） */
bool car_mgr_post_mode(CarStatus status, uint32_t source)
{
    if (!g_mode_queue_inited)
        return false;

    ModeCmdMsg msg = {.status = status, .source = source};

    uint32_t irq_sts = osal_irq_lock();
    int ret = osal_msgq_overwrite(g_mode_queue, MODE_CMD_QUEUE_DEPTH, &msg, sizeof(msg));
    osal_irq_restore(irq_sts);

    return (ret == OSAL_SUCCESS);
}

/* 线程安全地获取当前CarState快照 */
void car_mgr_get_state_copy(CarState *out)
{
    if (out == NULL)
        return;
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    *out = g_car_state;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

/* 更新全局状态中的超声波测距值 */
void car_mgr_update_distance(float distance)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.distance = distance;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

/* 更新全局状态中的三路循迹红外传感器状态 */
void car_mgr_update_ir_status(unsigned int left, unsigned int middle, unsigned int right)
{
    MUTEX_LOCK(g_state_mutex, g_state_mutex_inited);
    g_car_state.ir_left = left;
    g_car_state.ir_middle = middle;
    g_car_state.ir_right = right;
    MUTEX_UNLOCK(g_state_mutex, g_state_mutex_inited);
}

/* ============================================================
 * 统一初始化与任务入口
 * ============================================================ */

/* 统一初始化：驱动、互斥锁、WiFi、各服务、按键、模式队列 */
static void car_system_init(void)
{
    storage_service_init();

    // 驱动初始化（先于一切任务创建）
    l9110s_init();
    hcsr04_init();
    tcrt5000_adc_init();

    // 关键控制任务先创建，确保任务池不会因后续网络服务耗尽而抢占
    car_mgr_state_mutex_init();
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

    car_key_init();
    if (!g_mode_queue_inited) {
        if (osal_msg_queue_create("mode_q", MODE_CMD_QUEUE_DEPTH, &g_mode_queue, 0, sizeof(ModeCmdMsg)) == OSAL_SUCCESS)
            g_mode_queue_inited = true;
        else
            printf("CarMgr: 模式队列创建失败\r\n");
    }

    car_mgr_apply_status(CAR_STOP_STATUS);

    printf("Car: 系统初始化完成\r\n");
    printf("[FIRMWARE] OTA_TEST_BUILD_20250519_V2\r\n");
}

/**
 * @brief 主状态机任务 —— 纯事件驱动
 * @note 阻塞在模式切换消息队列上，无消息时永久休眠让出 CPU。
 *       只在收到模式切换请求时才执行 enter/exit 状态转移。
 */
static int car_main_task(void *arg)
{
    UNUSED(arg);

    car_system_init();

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
        if (!car_mgr_apply_status(msg.status))
            continue;

        // 执行状态转移
        switch (g_status) {
            case CAR_TRACE_STATUS:
                mode_obstacle_exit();
                mode_trace_enter();
                break;
            case CAR_OBSTACLE_AVOIDANCE_STATUS:
                mode_trace_exit();
                mode_obstacle_enter();
                break;
            default:
                mode_trace_exit();
                mode_obstacle_exit();
                break;
        }
    }

    return 0;
}

/* 应用入口：创建主状态机任务 */
static void car_demo_entry(void)
{
    (void)car_task_create_locked("car_main_task", (osal_kthread_handler)car_main_task, NULL, 4096, 25);
    printf("智能小车主任务已创建\r\n");
}

app_run(car_demo_entry);