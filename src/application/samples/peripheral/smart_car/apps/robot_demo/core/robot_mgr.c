#include "robot_mgr.h"

static CarStatus g_status = CAR_STOP_STATUS;

// 全局机器人状态，供 HTTP 服务读取
static RobotState g_robot_state = {0};

// 互斥锁保护状态访问
static osal_mutex g_state_mutex;
static bool g_state_mutex_inited = false;

/**
 * @brief 运行待机模式，显示 WiFi 连接状态和 IP 地址
 */
static void robot_mgr_run_standby(void)
{
    car_stop();

    while (robot_mgr_get_status() == CAR_STOP_STATUS) {
        char ip_line[32] = {0};
        const char *ip = net_service_get_ip();

        if (ip != NULL)
            (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip);
        else
            (void)snprintf(ip_line, sizeof(ip_line), "IP: Pending");

        ui_render_standby(net_service_is_connected() ? "WiFi: Connected" : "WiFi: Connecting", ip_line);
        osal_msleep(500);
    }

    car_stop();
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
    l9110s_init();
    hcsr04_init();
    tcrt5000_init();
    sg90_init();

    ui_service_init();
    net_service_init();
    http_ctrl_service_init();

    robot_mgr_state_mutex_init();
    robot_mgr_set_status(CAR_STOP_STATUS);

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
    g_status = status;

    // 更新状态中的模式
    if (g_state_mutex_inited)
        (void)osal_mutex_lock(&g_state_mutex);

    g_robot_state.mode = status;
    if (g_state_mutex_inited)
        osal_mutex_unlock(&g_state_mutex);

    ui_show_mode_page(status);
}

/**
 * @brief 主循环处理函数，根据当前状态执行相应模式的运行逻辑
 * @note 应在主任务中循环调用此函数
 */
void robot_mgr_process_loop(void)
{
    CarStatus status = robot_mgr_get_status();

    switch (status) {
        case CAR_STOP_STATUS:
            robot_mgr_run_standby();
            break;
        case CAR_TRACE_STATUS:
            mode_trace_run();
            break;
        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            mode_obstacle_run();
            break;
        case CAR_WIFI_CONTROL_STATUS:
            mode_remote_run();
            break;
        default:
            break;
    }
}

/**
 * @brief 更新舵机角度到全局状态
 * @param angle 舵机角度值（0-180度）
 */
void robot_mgr_update_servo_angle(unsigned int angle)
{
    if (g_state_mutex_inited)
        (void)osal_mutex_lock(&g_state_mutex);

    g_robot_state.servo_angle = angle;
    if (g_state_mutex_inited)
        osal_mutex_unlock(&g_state_mutex);
}

/**
 * @brief 更新超声波测距值到全局状态
 * @param distance 距离值（单位：厘米）
 */
void robot_mgr_update_distance(float distance)
{
    if (g_state_mutex_inited)
        (void)osal_mutex_lock(&g_state_mutex);

    g_robot_state.distance = distance;
    if (g_state_mutex_inited)
        osal_mutex_unlock(&g_state_mutex);
}

/**
 * @brief 更新红外传感器状态到全局状态
 * @param left 左侧红外传感器状态
 * @param middle 中间红外传感器状态
 * @param right 右侧红外传感器状态
 */
void robot_mgr_update_ir_status(unsigned int left, unsigned int middle, unsigned int right)
{
    if (g_state_mutex_inited)
        (void)osal_mutex_lock(&g_state_mutex);

    g_robot_state.ir_left = left;
    g_robot_state.ir_middle = middle;
    g_robot_state.ir_right = right;
    if (g_state_mutex_inited)
        osal_mutex_unlock(&g_state_mutex);
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

    if (g_state_mutex_inited)
        (void)osal_mutex_lock(&g_state_mutex);

    *out = g_robot_state;
    if (g_state_mutex_inited)
        osal_mutex_unlock(&g_state_mutex);
}
