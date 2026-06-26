/**
 * @file        storage_service.c
 * @brief       NV 存储服务实现
 * @details     提供 PID 参数和 WiFi 配置的持久化存储功能
 * @date        2025-02-03
 */

#include "storage_service.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../apps/car_demo/car_common.h"

#include "nv.h"
#include "securec.h"
#include "soc_osal.h"

/**
 * @brief 内部存储结构（包含校验头）
 * @details 使用 magic/version/checksum 确保数据有效性
 */
typedef struct {
    uint32_t magic;    // 魔术字，用于验证配置有效性 (0x524F4254 = "ROBT")
    uint16_t version;  // 配置版本号
    uint16_t checksum; // 16 位校验和（整个结构体累加，计算时此字段置 0）

    // PID 参数（使用整数存储，避免 float 二进制兼容问题）
    int32_t pid_kp_x1000;   // Kp * 1000
    int32_t pid_ki_x10000;  // Ki * 10000
    int32_t pid_kd_x500;    // Kd * 500
    int16_t pid_base_speed; // 基础速度

    // WiFi 配置
    char wifi_ssid[32];     // WiFi SSID
    char wifi_password[64]; // WiFi 密码

    // TCRT5000 阈值（复用原本 8 字节保留字段）
    uint16_t trace_l_threshold;
    uint16_t trace_m_threshold;
    uint16_t trace_r_threshold;
    uint8_t reserved[2]; // 剩余的保留字段
} car_nv_config_t;

#define CAR_NV_CONFIG_KEY ((uint16_t)0x2000)       // NV 存储键值
#define CAR_NV_CONFIG_MAGIC ((uint32_t)0x524F4254) // "ROBT"
#define CAR_NV_CONFIG_VERSION ((uint16_t)2)        // NV 配置结构体版本号

static car_nv_config_t g_nv_cfg = {0};      // NV 存储的配置数据
static osal_mutex g_storage_mutex;          // 保护 NV 存储访问的互斥锁
static bool g_storage_mutex_inited = false; // 互斥锁是否已初始化

// 使用 car_common.h 中的通用锁宏
#define STORAGE_LOCK() MUTEX_LOCK(g_storage_mutex, g_storage_mutex_inited)
#define STORAGE_UNLOCK() MUTEX_UNLOCK(g_storage_mutex, g_storage_mutex_inited)

/**
 * @brief NV 配置校验和计算（16 位累加）
 * @param data 数据指针
 * @param len 数据长度
 * @return 16 位校验和
 */
static uint16_t nv_checksum16_add(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFFu);
}

// 公共 NV 数据持久化模块。提取通用的校验和重算、写 Flash API
// 调用以及统一日志输出，消除了原本分散在各项配置保存函数中的高度重复代码。
static errcode_t nv_flush_config(void)
{
    g_nv_cfg.checksum = 0;
    g_nv_cfg.checksum = nv_checksum16_add((const uint8_t *)&g_nv_cfg, sizeof(g_nv_cfg));
    errcode_t ret = uapi_nv_write(CAR_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
    printf("[存储] NV 写入: 返回值=%d\r\n", ret);
    return ret;
}

/**
 * @brief 生成默认 NV 配置并计算校验
 * @param cfg 配置结构体指针
 */
static void nv_set_defaults(car_nv_config_t *cfg)
{
    (void)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->magic = CAR_NV_CONFIG_MAGIC;
    cfg->version = CAR_NV_CONFIG_VERSION;

    // PID 默认值
    cfg->pid_kp_x1000 = 21000; // Kp = 21.0
    cfg->pid_ki_x10000 = 0;    // Ki = 0.0
    cfg->pid_kd_x500 = 0;      // Kd = 0.0
    cfg->pid_base_speed = 80;

    // WiFi 默认值
    strncpy(cfg->wifi_ssid, "BSHZ-2.4G", 31);
    strncpy(cfg->wifi_password, "BS6668888", 63);

    // 循迹阈值默认值
    cfg->trace_l_threshold = 1750;
    cfg->trace_m_threshold = 1150;
    cfg->trace_r_threshold = 1600;

    cfg->checksum = 0;
    cfg->checksum = nv_checksum16_add((const uint8_t *)cfg, sizeof(*cfg));
}

/**
 * @brief 校验 NV 配置的有效性
 * @param cfg 配置结构体指针
 * @return true 配置有效，false 配置无效
 */
static bool nv_validate(car_nv_config_t *cfg)
{
    if (cfg->magic != CAR_NV_CONFIG_MAGIC || cfg->version != CAR_NV_CONFIG_VERSION) {
        return false;
    }
    uint16_t saved = cfg->checksum;
    cfg->checksum = 0;
    uint16_t calc = nv_checksum16_add((const uint8_t *)cfg, sizeof(*cfg));
    cfg->checksum = saved;
    return saved == calc;
}

/**
 * @brief 初始化存储服务互斥锁
 */
static void storage_mutex_init(void)
{
    if (g_storage_mutex_inited)
        return;
    if (osal_mutex_init(&g_storage_mutex) == OSAL_SUCCESS)
        g_storage_mutex_inited = true;
}

/**
 * @brief 初始化存储服务
 * @note 从 NV 存储加载配置，如果无效则使用默认值
 */
void storage_service_init(void)
{
    storage_mutex_init();

    (void)uapi_nv_init();

    STORAGE_LOCK();
    uint16_t out_len = 0;
    errcode_t ret = uapi_nv_read(CAR_NV_CONFIG_KEY, (uint16_t)sizeof(g_nv_cfg), &out_len, (uint8_t *)&g_nv_cfg);

    // 输出详细诊断日志
    printf("[存储] NV 读取: 返回值=%d, 长度=%d/%zu\r\n", ret, out_len, sizeof(g_nv_cfg));
    if (ret == ERRCODE_SUCC && out_len == sizeof(g_nv_cfg)) {
        printf("[存储] 魔术字=0x%X, 版本=%d, 校验和=0x%X\r\n", g_nv_cfg.magic, g_nv_cfg.version, g_nv_cfg.checksum);
        printf("[存储] WiFi SSID: %s\r\n", g_nv_cfg.wifi_ssid);
    }

    // 检查 NV 数据是否有效，无效则使用默认值
    if (ret != ERRCODE_SUCC || out_len != sizeof(g_nv_cfg) || !nv_validate(&g_nv_cfg)) {
        printf("[存储] NV 数据无效或不存在，使用默认值并写入\r\n");
        nv_set_defaults(&g_nv_cfg);
        ret = uapi_nv_write(CAR_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
        printf("[存储] NV 写入默认值: 返回值=%d\r\n", ret);
    } else {
        // 如果数据有效，但阈值是 0 或 0xFFFF (即升级前的未定义数据)，进行平滑升级赋初值
        bool updated = false;
        if (g_nv_cfg.trace_l_threshold == 0 || g_nv_cfg.trace_l_threshold == 0xFFFF) {
            g_nv_cfg.trace_l_threshold = 1750;
            updated = true;
        }
        if (g_nv_cfg.trace_m_threshold == 0 || g_nv_cfg.trace_m_threshold == 0xFFFF) {
            g_nv_cfg.trace_m_threshold = 1150;
            updated = true;
        }
        if (g_nv_cfg.trace_r_threshold == 0 || g_nv_cfg.trace_r_threshold == 0xFFFF) {
            g_nv_cfg.trace_r_threshold = 1600;
            updated = true;
        }
        if (updated) {
            ret = nv_flush_config();
            printf("[存储] 检测到旧版本 NV 配置，已升级循迹阈值默认值并写入，Ret=%d\r\n", ret);
        }
        printf("[存储] 加载 NV 配置成功\r\n");
    }
    STORAGE_UNLOCK();
}

/**
 * @brief 获取 PID 参数
 */
void storage_service_get_pid_params(float *kp, float *ki, float *kd, int16_t *speed)
{
    if (kp == NULL || ki == NULL || kd == NULL || speed == NULL)
        return;

    STORAGE_LOCK();
    *kp = (float)g_nv_cfg.pid_kp_x1000 / 1000.0f;
    *ki = (float)g_nv_cfg.pid_ki_x10000 / 10000.0f;
    *kd = (float)g_nv_cfg.pid_kd_x500 / 500.0f;
    *speed = g_nv_cfg.pid_base_speed;
    STORAGE_UNLOCK();
}

/**
 * @brief 保存 PID 参数到 NV
 */
errcode_t storage_service_save_pid_params(float kp, float ki, float kd, int16_t speed)
{
    int32_t kp_x1000 = (int32_t)(kp * 1000.0f);
    int32_t ki_x10000 = (int32_t)(ki * 10000.0f);
    int32_t kd_x500 = (int32_t)(kd * 500.0f);

    // 防止重复保存
    STORAGE_LOCK();
    if (g_nv_cfg.pid_kp_x1000 == kp_x1000 && g_nv_cfg.pid_ki_x10000 == ki_x10000 && g_nv_cfg.pid_kd_x500 == kd_x500 &&
        g_nv_cfg.pid_base_speed == speed) {
        STORAGE_UNLOCK();
        return ERRCODE_SUCC;
    }

    g_nv_cfg.pid_kp_x1000 = kp_x1000;
    g_nv_cfg.pid_ki_x10000 = ki_x10000;
    g_nv_cfg.pid_kd_x500 = kd_x500;
    g_nv_cfg.pid_base_speed = speed;

    printf("[存储] 保存 PID: Kp=%d/1000, Ki=%d/10000, Kd=%d/500, 基础速度=%d\r\n", (int)(kp * 1000), (int)(ki * 10000),
           (int)(kd * 500), speed);
    errcode_t ret = nv_flush_config();
    STORAGE_UNLOCK();

    return ret;
}

/**
 * @brief 获取 WiFi 配置
 */
void storage_service_get_wifi_config(char *ssid, char *password)
{
    if (ssid == NULL || password == NULL)
        return;

    STORAGE_LOCK();
    (void)strncpy_s(ssid, 32, g_nv_cfg.wifi_ssid, 31);
    (void)strncpy_s(password, 64, g_nv_cfg.wifi_password, 63);
    STORAGE_UNLOCK();
}

/**
 * @brief 保存 WiFi 配置到 NV
 */
errcode_t storage_service_save_wifi_config(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL)
        return ERRCODE_INVALID_PARAM;

    STORAGE_LOCK();

    // 防止重复保存
    if (strcmp(g_nv_cfg.wifi_ssid, ssid) == 0 && strcmp(g_nv_cfg.wifi_password, password) == 0) {
        STORAGE_UNLOCK();
        return ERRCODE_SUCC;
    }

    (void)strncpy_s(g_nv_cfg.wifi_ssid, sizeof(g_nv_cfg.wifi_ssid), ssid, sizeof(g_nv_cfg.wifi_ssid) - 1);
    (void)strncpy_s(g_nv_cfg.wifi_password, sizeof(g_nv_cfg.wifi_password), password,
                    sizeof(g_nv_cfg.wifi_password) - 1);

    printf("[存储] 保存 WiFi: SSID='%s', 密码长度=%zu\r\n", ssid, strlen(password));
    errcode_t ret = nv_flush_config();
    STORAGE_UNLOCK();
    return ret;
}

/**
 * @brief 获取循迹传感器阈值
 */
void storage_service_get_trace_thresholds(uint16_t *l, uint16_t *m, uint16_t *r)
{
    if (l == NULL || m == NULL || r == NULL) {
        return;
    }

    STORAGE_LOCK();
    *l = g_nv_cfg.trace_l_threshold;
    *m = g_nv_cfg.trace_m_threshold;
    *r = g_nv_cfg.trace_r_threshold;
    STORAGE_UNLOCK();
}

/**
 * @brief 保存循迹传感器阈值
 */
errcode_t storage_service_save_trace_thresholds(uint16_t l, uint16_t m, uint16_t r)
{
    STORAGE_LOCK();
    if (g_nv_cfg.trace_l_threshold == l && g_nv_cfg.trace_m_threshold == m && g_nv_cfg.trace_r_threshold == r) {
        STORAGE_UNLOCK();
        return ERRCODE_SUCC;
    }

    g_nv_cfg.trace_l_threshold = l;
    g_nv_cfg.trace_m_threshold = m;
    g_nv_cfg.trace_r_threshold = r;

    printf("[存储] 保存循迹阈值: L=%d, M=%d, R=%d\r\n", l, m, r);
    errcode_t ret = nv_flush_config();
    STORAGE_UNLOCK();

    return ret;
}
