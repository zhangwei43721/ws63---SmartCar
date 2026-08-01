/**
 ****************************************************************************************************
 * @file        car_main.c
 * @author      SkyForever
 * @version     V1.2
 * @date        2025-01-16
 * @brief       LiteOS 智能小车主入口：初始化编排 + 按键中断（状态机本体见 core/mode_mgr.c）
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 */

#include "app_init.h"
#include "common_def.h"
#include "car_common.h"
#include "core/car_ctrl.h"
#include "core/car_state.h"
#include "core/mode_mgr.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "pinctrl.h"
#include "services/captive_portal_service.h"
#include "services/debug_log_service.h"
#include "services/ota_service.h"
#include "services/ui_service.h"
#include "services/wifi_mgr_service.h"
#include "channels/sle_channel.h"
#include "channels/udp_channel.h"
#include "channels/voice_channel.h"
#include "../../platform/storage_service.h"
#include "soc_osal.h"

#include "../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../drivers/l9110s/bsp_l9110s.h"
#include "../../drivers/motor_control/bsp_motor.h"
#include "../../drivers/tcrt5000/bsp_tcrt5000.h"

/* ============================================================
 * 按键中断
 * ============================================================ */

static unsigned long long button_time_tick = 0;

// 按键中断回调：200ms消抖后循环切换小车模式
static void mode_switch_isr(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);

    unsigned long long current_tick = osal_get_jiffies();
    if ((current_tick - button_time_tick) < osal_msecs_to_jiffies(200))
        return;

    button_time_tick = current_tick;

    CarStatus next_status = (CarStatus)((mode_mgr_current() + 1) % 4);
    mode_mgr_post(next_status, MODE_SRC_BUTTON);
}

// 初始化模式切换按键GPIO及下降沿中断
static void car_key_init(void)
{
    uapi_pin_set_mode(3, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(3, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(3, PIN_PULL_TYPE_UP);

    uapi_gpio_register_isr_func(3, GPIO_INTERRUPT_FALLING_EDGE, mode_switch_isr);
}

/* ============================================================
 * 统一初始化与任务入口
 * ============================================================ */

// 统一初始化：状态仓库、模式队列、驱动、WiFi、各通道与服务、按键
static void car_system_init(void)
{
    // 状态仓库与模式队列最先就绪，此后任何通道投递都不会被静默丢弃
    car_state_init();
    mode_mgr_init();
    car_ctrl_init(); // 统一命令总线（须在通道注册前就绪）

    storage_service_init();
    debug_log_init();

    // 载入 NV 中的校准阈值并同步到状态仓库
    uint16_t th_l, th_m, th_r;
    storage_service_get_trace_thresholds(&th_l, &th_m, &th_r);
    car_state_update_thresholds(th_l, th_m, th_r);

    // 驱动初始化（先于一切任务创建）
    l9110s_init();
    hcsr04_init();
    tcrt5000_adc_init();

    // 关键控制任务先创建，确保任务池不会因后续网络服务耗尽而抢占
    bsp_motor_init();

    // WiFi 管理任务
    wifi_mgr_init();
    wifi_mgr_msg_t wifi_start = {.id = WIFI_MSG_START};
    wifi_mgr_send_msg(&wifi_start);

    // 服务（UI / OTA / 强制门户）
    ui_service_init();
    ota_service_init();
    captive_portal_service_init();

    // 控制通道（UDP / SLE / 语音）
    udp_channel_init();
    sle_channel_init();
    voice_channel_init();

    car_key_init();

    printf("Car: 系统初始化完成\r\n");
    printf("[FIRMWARE] OTA_TEST_BUILD_20250519_V2\r\n");
}

// 主任务：完成系统初始化后，把执行权交给模式状态机主循环
static int car_main_task(void *arg)
{
    UNUSED(arg);

    car_system_init();
    mode_mgr_run(); // 阻塞于模式切换队列，不返回

    return 0;
}

// 应用入口：创建主状态机任务
static void car_demo_entry(void)
{
    (void)car_task_create_locked("car_main_task", (osal_kthread_handler)car_main_task, NULL, 4096, 25);
    printf("智能小车主任务已创建\r\n");
}

app_run(car_demo_entry);
