#include "robot_mgr.h"
#include "robot_config.h"
#include "mode_obstacle.h"
#include "mode_remote.h"
#include "mode_trace.h"

#include "../services/ui_service.h"
#include "../services/udp_service.h"
#include "../services/storage_service.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include "securec.h"
#include "soc_osal.h"

#include <stdbool.h>
#include <stdio.h>

static CarStatus g_status = CAR_STOP_STATUS;
static CarStatus g_last_status = CAR_STOP_STATUS;

// =============== 互斥锁操作宏 ===============
#define ROBOT_STATE_LOCK()                         \
    do {                                           \
        if (g_state_mutex_inited)                  \
            (void)osal_mutex_lock(&g_state_mutex); \
    } while (0)

#define ROBOT_STATE_UNLOCK()                   \
    do {                                       \
        if (g_state_mutex_inited)              \
            osal_mutex_unlock(&g_state_mutex); \
    } while (0)

#define UPDATE_STATE_FIELD(field, value) \
    do {                                 \
        ROBOT_STATE_LOCK();              \
        g_robot_state.field = value;     \
        ROBOT_STATE_UNLOCK();            \
    } while (0)

// 全局机器人状态，供 HTTP 服务读取
static RobotState g_robot_state = {0};

// 互斥锁保护状态访问
static osal_mutex g_state_mutex;
static bool g_state_mutex_inited = false;

// 模式操作接口定义
static RobotModeOps g_mode_ops[] = {
    // CAR_STOP_STATUS (0)
    { NULL, NULL, NULL },
    // CAR_TRACE_STATUS (1)
    { mode_trace_enter, mode_trace_tick, mode_trace_exit },
    // CAR_OBSTACLE_AVOIDANCE_STATUS (2)
    { mode_obstacle_enter, mode_obstacle_tick, mode_obstacle_exit },
    // CAR_WIFI_CONTROL_STATUS (3)
    { mode_remote_enter, mode_remote_tick, mode_remote_exit }
};

/**
 * @brief 运行待机模式，显示 WiFi 连接状态和 IP 地址
 * @note 待机模式由主 tick 循环直接处理，不通过 ops 接口（为了简化 UI 更新逻辑）
 */
static void robot_mgr_run_standby_tick(void)
{
    static unsigned long long last_ui_update = 0;
    unsigned long long now = osal_get_jiffies();

    // 只有在切换到待机时停止一次
    if (g_last_status != CAR_STOP_STATUS) {
        car_stop();
    }

    if (now - last_ui_update >= osal_msecs_to_jiffies(STANDBY_UPDATE_DELAY_MS)) {
        char ip_line[IP_BUFFER_SIZE] = {0};
        const char *ip = udp_service_get_ip();

        if (ip != NULL)
            (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip);
        else
            (void)snprintf(ip_line, sizeof(ip_line), "IP: Pending");

        ui_render_standby(udp_service_is_connected() ? "WiFi: Connected" : "WiFi: Connecting", ip_line);
        last_ui_update = now;
    }
}

/**
 * @brief 初始化状态互斥锁，保护全局机器人状态的并发访问
 */
static void robot_mgr_state_mutex_init(void)
{
    if (g_state_mutex_inited)
        return;

    if (osal_mutex_init(&g_state_mutex) == OSAL_SUCCESS)
        g_state_mutex_inited = true;
    else
        printf("RobotMgr: 状态互斥锁初始化失败\r\n");
}

/**
 * @brief 初始化机器人管理器，包括所有硬件驱动和服务
 * @note 初始化电机、超声波、红外、舵机驱动，以及网络、UI、HTTP 服务
 */
void robot_mgr_init(void)
{
    // 优先加载运行参数，供后续模式逻辑读取（避障阈值/舵机回中等）
    storage_service_init();

    l9110s_init();
    hcsr04_init();
    tcrt5000_init();
    sg90_init();

    ui_service_init();
    // net_service_init();
    // http_ctrl_service_init();
    udp_service_init();
    robot_mgr_state_mutex_init();
    robot_mgr_set_status(CAR_STOP_STATUS);
    g_last_status = CAR_STOP_STATUS;

    printf("RobotMgr: 初始化完成\r\n");
}

/**
 * @brief 获取当前小车状态
 * @return 当前状态枚举值（停止、循迹、避障、WiFi 控制等）
 */
CarStatus robot_mgr_get_status(void)
{
    return g_status;
}

/**
 * @brief 设置小车状态并更新 UI 显示
 * @param status 新的状态值
 */
void robot_mgr_set_status(CarStatus status)
{
    if (g_status != status) {
        g_status = status;
        UPDATE_STATE_FIELD(mode, status);
        ui_show_mode_page(status);
    }
}

/**
 * @brief 周期性调用函数，处理模式生命周期和状态机
 */
void robot_mgr_tick(void)
{
    CarStatus current_status = g_status;

    // 1. 处理状态切换
    if (current_status != g_last_status) {
        // 退出旧模式
        if (g_last_status > CAR_STOP_STATUS && g_last_status < sizeof(g_mode_ops)/sizeof(g_mode_ops[0])) {
            if (g_mode_ops[g_last_status].exit) {
                g_mode_ops[g_last_status].exit();
            }
        }
        
        // 进入新模式
        if (current_status > CAR_STOP_STATUS && current_status < sizeof(g_mode_ops)/sizeof(g_mode_ops[0])) {
            if (g_mode_ops[current_status].enter) {
                g_mode_ops[current_status].enter();
            }
        }
        
        g_last_status = current_status;
    }

    // 2. 执行当前模式逻辑
    if (current_status == CAR_STOP_STATUS) {
        robot_mgr_run_standby_tick();
    } else if (current_status > CAR_STOP_STATUS && current_status < sizeof(g_mode_ops)/sizeof(g_mode_ops[0])) {
        if (g_mode_ops[current_status].tick) {
            g_mode_ops[current_status].tick();
        }
    }
}

/**
 * @brief 更新舵机角度到全局状态
 * @param angle 舵机角度值（0-180度）
 */
void robot_mgr_update_servo_angle(unsigned int angle)
{
    UPDATE_STATE_FIELD(servo_angle, angle);
}

/**
 * @brief 更新超声波测距值到全局状态
 * @param distance 距离值（单位：厘米）
 */
void robot_mgr_update_distance(float distance)
{
    UPDATE_STATE_FIELD(distance, distance);
}

/**
 * @brief 更新红外传感器状态到全局状态
 * @param left 左侧红外传感器状态
 * @param middle 中间红外传感器状态
 * @param right 右侧红外传感器状态
 */
void robot_mgr_update_ir_status(unsigned int left, unsigned int middle, unsigned int right)
{
    ROBOT_STATE_LOCK();
    g_robot_state.ir_left = left;
    g_robot_state.ir_middle = middle;
    g_robot_state.ir_right = right;
    ROBOT_STATE_UNLOCK();
}

/**
 * @brief 获取全局机器人状态的副本
 * @param out 输出参数，用于接收状态副本
 * @note 此函数线程安全，使用互斥锁保护数据读取
 */
void robot_mgr_get_state_copy(RobotState *out)
{
    if (out == NULL)
        return;

    ROBOT_STATE_LOCK();
    *out = g_robot_state;
    ROBOT_STATE_UNLOCK();
}

unsigned int robot_mgr_get_obstacle_threshold_cm(void)
{
    return (unsigned int)storage_service_get_obstacle_threshold();
}

unsigned int robot_mgr_get_servo_center_angle(void)
{
    return (unsigned int)storage_service_get_servo_center();
}
