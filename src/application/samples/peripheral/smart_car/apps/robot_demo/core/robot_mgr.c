#include "robot_mgr.h"
#include "robot_config.h"
#include "mode_obstacle.h"
#include "mode_remote.h"
#include "mode_trace.h"

#include "../services/ui_service.h"
#include "../services/udp_service.h"

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../../drivers/l9110s/bsp_l9110s.h"
#include "../../../drivers/sg90/bsp_sg90.h"
#include "../../../drivers/tcrt5000/bsp_tcrt5000.h"

#include "nv.h"
#include "securec.h"
#include "soc_osal.h"

#include <stdbool.h>
#include <stdio.h>

static CarStatus g_status = CAR_STOP_STATUS;

// 机器人运行参数（保存到 NV 的轻量配置）
// - magic/version: 结构有效性标识
// - checksum: 对整个结构体做 16-bit 累加校验（校验时将 checksum 置 0）
// - obstacle_threshold_cm: 避障阈值（cm）
// - servo_center_angle: 舵机回中角度（0~180）
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t checksum;
    uint16_t obstacle_threshold_cm;
    uint16_t servo_center_angle;
    uint8_t reserved[16];
} robot_nv_config_t;

#define ROBOT_NV_CONFIG_KEY ((uint16_t)0x2000)
#define ROBOT_NV_CONFIG_MAGIC ((uint32_t)0x524F4254)
#define ROBOT_NV_CONFIG_VERSION ((uint16_t)1)

static robot_nv_config_t g_nv_cfg = {0};

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

// NV 配置校验：简单累加，足够检测大部分写入异常/脏数据
static uint16_t nv_checksum16_add(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFFu);
}

// 生成默认 NV 配置并计算校验
static void nv_set_defaults(robot_nv_config_t *cfg)
{
    (void)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->magic = ROBOT_NV_CONFIG_MAGIC;
    cfg->version = ROBOT_NV_CONFIG_VERSION;
    cfg->obstacle_threshold_cm = DISTANCE_BETWEEN_CAR_AND_OBSTACLE;
    cfg->servo_center_angle = SERVO_MIDDLE_ANGLE;
    cfg->checksum = 0;
    cfg->checksum = nv_checksum16_add((const uint8_t *)cfg, sizeof(*cfg));
}

// 校验 magic/version 与 checksum，避免使用损坏配置
static bool nv_validate(robot_nv_config_t *cfg)
{
    if (cfg->magic != ROBOT_NV_CONFIG_MAGIC || cfg->version != ROBOT_NV_CONFIG_VERSION) {
        return false;
    }
    uint16_t saved = cfg->checksum;
    cfg->checksum = 0;
    uint16_t calc = nv_checksum16_add((const uint8_t *)cfg, sizeof(*cfg));
    cfg->checksum = saved;
    return saved == calc;
}

// 从 NV 读取配置；读取失败或校验失败则写入默认值
static void nv_load_or_init(void)
{
    (void)uapi_nv_init();

    uint16_t out_len = 0;
    errcode_t ret = uapi_nv_read(ROBOT_NV_CONFIG_KEY, (uint16_t)sizeof(g_nv_cfg), &out_len, (uint8_t *)&g_nv_cfg);
    if (ret != ERRCODE_SUCC || out_len != sizeof(g_nv_cfg) || !nv_validate(&g_nv_cfg)) {
        nv_set_defaults(&g_nv_cfg);
        (void)uapi_nv_write(ROBOT_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
    }
}

/**
 * @brief 运行待机模式，显示 WiFi 连接状态和 IP 地址
 */
static void robot_mgr_run_standby(void)
{
    car_stop();

    while (robot_mgr_get_status() == CAR_STOP_STATUS) {
        char ip_line[IP_BUFFER_SIZE] = {0};
        const char *ip = udp_service_get_ip();

        if (ip != NULL)
            (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip);
        else
            (void)snprintf(ip_line, sizeof(ip_line), "IP: Pending");

        ui_render_standby(udp_service_is_connected() ? "WiFi: Connected" : "WiFi: Connecting", ip_line);
        osal_msleep(STANDBY_UPDATE_DELAY_MS);
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
    // 优先加载运行参数，供后续模式逻辑读取（避障阈值/舵机回中等）
    nv_load_or_init();

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
    UPDATE_STATE_FIELD(mode, status);
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
    unsigned int cm = (unsigned int)g_nv_cfg.obstacle_threshold_cm;
    if (cm == 0) {
        cm = DISTANCE_BETWEEN_CAR_AND_OBSTACLE;
    }
    return cm;
}

unsigned int robot_mgr_get_servo_center_angle(void)
{
    unsigned int a = (unsigned int)g_nv_cfg.servo_center_angle;
    if (a > 180)
        a = SERVO_MIDDLE_ANGLE;
    return a;
}
