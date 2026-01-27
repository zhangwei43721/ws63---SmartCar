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

static CarStatus g_status = CAR_STOP_STATUS;      /* 当前小车运行模式 */
static CarStatus g_last_status = CAR_STOP_STATUS; /* 上次小车运行模式（用于检测模式切换） */

// 全局机器人状态（包含舵机角度、距离、传感器值等）
static RobotState g_robot_state = {0};

// 互斥锁：保护 机器人状态，防止多个线程同时读写
static osal_mutex g_state_mutex;
// 互斥锁是否已初始化的标志（初始化成功后设为 true）
static bool g_state_mutex_inited = false;

// 模式操作接口定义（按 CarStatus 枚举值索引）
static RobotModeOps g_mode_ops[] = {
    // CAR_STOP_STATUS (0)
    {NULL, NULL, NULL},
    // CAR_TRACE_STATUS (1)
    {mode_trace_enter, mode_trace_tick, mode_trace_exit},
    // CAR_OBSTACLE_AVOIDANCE_STATUS (2)
    {mode_obstacle_enter, mode_obstacle_tick, mode_obstacle_exit},
    // CAR_WIFI_CONTROL_STATUS (3)
    {mode_remote_enter, mode_remote_tick, mode_remote_exit}};

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
        CAR_STOP();
    }

    if (now - last_ui_update >= osal_msecs_to_jiffies(STANDBY_DELAY)) {
        char ip_line[BUF_IP] = {0};
        const char *ip = udp_service_get_ip();

        // 获取 IP 地址字符串
        if (ip != NULL)
            (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip);
        else
            (void)snprintf(ip_line, sizeof(ip_line), "IP: Pending");

        // 获取 WiFi 连接状态
        WifiConnectStatus wifi_status = udp_service_get_wifi_status();
        ui_render_standby(wifi_status, ip_line);       
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
        // 加锁保护状态更新
        if (g_state_mutex_inited) {
            osal_mutex_lock(&g_state_mutex);
            g_robot_state.mode = status;
            osal_mutex_unlock(&g_state_mutex);
        }
        ui_show_mode_page(status);
    }
}

/**
 * @brief 周期性调用函数，处理模式生命周期和状态机
 */
void robot_mgr_tick(void)
{
    CarStatus current_status = g_status; // 当前状态
    int mode_count = (int)(sizeof(g_mode_ops) / sizeof(g_mode_ops[0])); // 模式数量

    // 1. 处理状态切换
    if (current_status != g_last_status) {
        // 退出旧模式
        if (g_last_status > CAR_STOP_STATUS && g_last_status < mode_count) {
            if (g_mode_ops[g_last_status].exit)
                g_mode_ops[g_last_status].exit();
        }

        // 进入新模式
        if (current_status > CAR_STOP_STATUS && current_status < mode_count) {
            if (g_mode_ops[current_status].enter)
                g_mode_ops[current_status].enter();
        }

        g_last_status = current_status;
    }

    // 2. 执行当前模式逻辑
    if (current_status == CAR_STOP_STATUS) {
        robot_mgr_run_standby_tick();
    } else if (current_status > CAR_STOP_STATUS && current_status < mode_count) {
        if (g_mode_ops[current_status].tick)
            g_mode_ops[current_status].tick();
    }
}

/**
 * @brief 更新舵机角度到全局状态
 * @param angle 舵机角度值（0-180度）
 */
void robot_mgr_update_servo_angle(unsigned int angle)
{
    // 加锁保护状态更新
    if (g_state_mutex_inited) {
        osal_mutex_lock(&g_state_mutex);
        g_robot_state.servo_angle = angle;
        osal_mutex_unlock(&g_state_mutex);
    }
}

/**
 * @brief 更新超声波测距值到全局状态
 * @param distance 距离值（单位：厘米）
 */
void robot_mgr_update_distance(float distance)
{
    // 加锁保护状态更新
    if (g_state_mutex_inited) {
        osal_mutex_lock(&g_state_mutex);
        g_robot_state.distance = distance;
        osal_mutex_unlock(&g_state_mutex);
    }
}

/**
 * @brief 更新红外传感器状态到全局状态
 * @param left 左侧红外传感器状态
 * @param middle 中间红外传感器状态
 * @param right 右侧红外传感器状态
 */
void robot_mgr_update_ir_status(unsigned int left, unsigned int middle, unsigned int right)
{
    // 加锁保护状态更新
    if (g_state_mutex_inited) {
        osal_mutex_lock(&g_state_mutex);
        g_robot_state.ir_left = left;
        g_robot_state.ir_middle = middle;
        g_robot_state.ir_right = right;
        osal_mutex_unlock(&g_state_mutex);
    }
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

    // 加锁保护状态读取
    if (g_state_mutex_inited) {
        osal_mutex_lock(&g_state_mutex);
        *out = g_robot_state;
        osal_mutex_unlock(&g_state_mutex);
    }
}
