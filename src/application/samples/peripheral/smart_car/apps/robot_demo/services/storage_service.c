#include "storage_service.h"
#include "../core/robot_config.h"
#include "nv.h"
#include "securec.h"
#include "soc_osal.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// 内部存储结构（包含校验头）
// - magic/version: 结构有效性标识
// - checksum: 对整个结构体做 16-bit 累加校验（校验时将 checksum 置 0）
typedef struct {
    uint32_t magic;          // 魔术字，用于验证配置有效性 (0x524F4254 = "ROBT")
    uint16_t version;        // 配置版本号
    uint16_t checksum;       // 16 位校验和（整个结构体累加，计算时此字段置 0）

    // PID 参数 (使用整数存储，避免 float 二进制兼容问题)
    int32_t pid_kp_x1000;    // Kp * 1000
    int32_t pid_ki_x10000;   // Ki * 10000
    int32_t pid_kd_x500;     // Kd * 500
    int16_t pid_base_speed;  // 基础速度

    // WiFi 配置
    char wifi_ssid[32];      // WiFi SSID
    char wifi_password[64];  // WiFi 密码

    uint8_t reserved[8];     // 保留字段，用于未来扩展
} robot_nv_config_t;

#define ROBOT_NV_CONFIG_KEY ((uint16_t)0x2000)
#define ROBOT_NV_CONFIG_MAGIC ((uint32_t)0x524F4254) // "ROBT"
#define ROBOT_NV_CONFIG_VERSION ((uint16_t)2)

static robot_nv_config_t g_nv_cfg = {0};    /* NV 存储的配置数据 */
static osal_mutex g_storage_mutex;          /* 保护 NV 存储访问的互斥锁 */
static bool g_storage_mutex_inited = false; /* 互斥锁是否已初始化 */

// 使用 robot_config.h 中的通用锁宏
#define STORAGE_LOCK()    MUTEX_LOCK(g_storage_mutex, g_storage_mutex_inited)
#define STORAGE_UNLOCK()  MUTEX_UNLOCK(g_storage_mutex, g_storage_mutex_inited)

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

/**
 * @brief 生成默认 NV 配置并计算校验
 * @param cfg 配置结构体指针
 */
static void nv_set_defaults(robot_nv_config_t *cfg)
{
    (void)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->magic = ROBOT_NV_CONFIG_MAGIC;
    cfg->version = ROBOT_NV_CONFIG_VERSION;

    // PID 默认值
    cfg->pid_kp_x1000 = 16000;       // Kp = 16.0
    cfg->pid_ki_x10000 = 0;          // Ki = 0.0
    cfg->pid_kd_x500 = 0;            // Kd = 0.0
    cfg->pid_base_speed = 40;

    // WiFi 默认值
    strncpy(cfg->wifi_ssid, "BSHZ-2.4G", 31);
    strncpy(cfg->wifi_password, "BS666888", 63);

    cfg->checksum = 0;
    cfg->checksum = nv_checksum16_add((const uint8_t *)cfg, sizeof(*cfg));
}

/**
 * @brief 校验 NV 配置的有效性
 * @param cfg 配置结构体指针
 * @return true 配置有效，false 配置无效
 */
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
    errcode_t ret = uapi_nv_read(ROBOT_NV_CONFIG_KEY, (uint16_t)sizeof(g_nv_cfg), &out_len, (uint8_t *)&g_nv_cfg);
    if (ret != ERRCODE_SUCC || out_len != sizeof(g_nv_cfg) || !nv_validate(&g_nv_cfg)) {
        printf("Storage: NV 数据无效或不存在，使用默认值并写入\r\n");
        nv_set_defaults(&g_nv_cfg);
        (void)uapi_nv_write(ROBOT_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
    } else {
        printf("Storage: 加载 NV 配置成功\r\n");
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
    STORAGE_LOCK();
    g_nv_cfg.pid_kp_x1000 = (int32_t)(kp * 1000.0f);
    g_nv_cfg.pid_ki_x10000 = (int32_t)(ki * 10000.0f);
    g_nv_cfg.pid_kd_x500 = (int32_t)(kd * 500.0f);
    g_nv_cfg.pid_base_speed = speed;

    // 重新计算校验和
    g_nv_cfg.checksum = 0;
    g_nv_cfg.checksum = nv_checksum16_add((const uint8_t *)&g_nv_cfg, sizeof(g_nv_cfg));

    errcode_t ret = uapi_nv_write(ROBOT_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
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
    strncpy(ssid, g_nv_cfg.wifi_ssid, 31);
    ssid[31] = '\0';
    strncpy(password, g_nv_cfg.wifi_password, 63);
    password[63] = '\0';
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
    strncpy(g_nv_cfg.wifi_ssid, ssid, 31);
    g_nv_cfg.wifi_ssid[31] = '\0';
    strncpy(g_nv_cfg.wifi_password, password, 63);
    g_nv_cfg.wifi_password[63] = '\0';

    // 重新计算校验和
    g_nv_cfg.checksum = 0;
    g_nv_cfg.checksum = nv_checksum16_add((const uint8_t *)&g_nv_cfg, sizeof(g_nv_cfg));

    errcode_t ret = uapi_nv_write(ROBOT_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
    STORAGE_UNLOCK();

    return ret;
}
