#include "storage_service.h"
#include "../core/robot_config.h"
#include "nv.h"
#include "securec.h"
#include "soc_osal.h"
#include <stdbool.h>
#include <stdio.h>

// 内部存储结构（包含校验头）
// - magic/version: 结构有效性标识
// - checksum: 对整个结构体做 16-bit 累加校验（校验时将 checksum 置 0）
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t checksum;
    uint16_t obstacle_threshold_cm;
    uint16_t servo_center_angle;
    uint8_t reserved[16];
} robot_nv_config_t;

#define ROBOT_NV_CONFIG_KEY ((uint16_t)0x2000)
#define ROBOT_NV_CONFIG_MAGIC ((uint32_t)0x524F4254) // "ROBT"
#define ROBOT_NV_CONFIG_VERSION ((uint16_t)1)

static robot_nv_config_t g_nv_cfg = {0};
static osal_mutex g_storage_mutex;
static bool g_storage_mutex_inited = false;

#define STORAGE_LOCK()                               \
    do {                                             \
        if (g_storage_mutex_inited)                  \
            (void)osal_mutex_lock(&g_storage_mutex); \
    } while (0)

#define STORAGE_UNLOCK()                         \
    do {                                         \
        if (g_storage_mutex_inited)              \
            osal_mutex_unlock(&g_storage_mutex); \
    } while (0)

// NV 配置校验：简单累加
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

// 校验 magic/version 与 checksum
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

static void storage_mutex_init(void)
{
    if (g_storage_mutex_inited)
        return;
    if (osal_mutex_init(&g_storage_mutex) == OSAL_SUCCESS)
        g_storage_mutex_inited = true;
}

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

void storage_service_get_params(robot_params_t *out_params)
{
    if (out_params == NULL)
        return;

    STORAGE_LOCK();
    out_params->obstacle_threshold_cm = g_nv_cfg.obstacle_threshold_cm;
    out_params->servo_center_angle = g_nv_cfg.servo_center_angle;
    STORAGE_UNLOCK();
}

errcode_t storage_service_save_params(const robot_params_t *params)
{
    if (params == NULL)
        return ERRCODE_INVALID_PARAM;

    STORAGE_LOCK();
    g_nv_cfg.obstacle_threshold_cm = params->obstacle_threshold_cm;
    g_nv_cfg.servo_center_angle = params->servo_center_angle;

    // 重新计算校验和
    g_nv_cfg.checksum = 0;
    g_nv_cfg.checksum = nv_checksum16_add((const uint8_t *)&g_nv_cfg, sizeof(g_nv_cfg));

    errcode_t ret = uapi_nv_write(ROBOT_NV_CONFIG_KEY, (const uint8_t *)&g_nv_cfg, (uint16_t)sizeof(g_nv_cfg));
    STORAGE_UNLOCK();

    return ret;
}

uint16_t storage_service_get_obstacle_threshold(void)
{
    uint16_t val;
    STORAGE_LOCK();
    val = g_nv_cfg.obstacle_threshold_cm;
    if (val == 0)
        val = DISTANCE_BETWEEN_CAR_AND_OBSTACLE;
    STORAGE_UNLOCK();
    return val;
}

uint16_t storage_service_get_servo_center(void)
{
    uint16_t val;
    STORAGE_LOCK();
    val = g_nv_cfg.servo_center_angle;
    if (val > 180)
        val = SERVO_MIDDLE_ANGLE;
    STORAGE_UNLOCK();
    return val;
}
