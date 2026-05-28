/**
 ****************************************************************************************************
 * @file        bsp_bt_spp.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
 * @brief       蓝牙SPP BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 * 使用说明:
 * 1. 服务UUID: 0xABCD (0000ABCD-0000-1000-8000-00805F9B34FB)
 * 2. 特征UUID: 0xCDEF (0000CDEF-0000-1000-8000-00805F9B34FB) -
 * 支持Read/Write/Notify
 * 3. 客户端需要先写入CCCD启用Notify才能接收数据
 *
 ****************************************************************************************************
 */

#include "bsp_bt_spp.h"

#include "../../board_config.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_init.h"
#include "bts_def.h"
#include "bts_gatt_client.h"
#include "bts_gatt_server.h"
#include "bts_gatt_stru.h"
#include "bts_le_gap.h"
#include "errcode.h"
#include "osal_addr.h"
#include "securec.h"
#include "soc_osal.h"
#include "std_def.h"
#include "systick.h"

// ==================== 常量定义 ====================
#define OCTET_BIT_LEN 8 // 字节位数
#define UUID_LEN_2 2    // 2 字节 UUID 长度

// ==================== 用户UUID配置 ====================
// 服务 UUID: 0000ABCD-...
#define BSP_BT_SPP_SERVICE_UUID 0xABCD
// 特征 UUID: 0000CDEF-... (读+写+通知) <-- 操作这个！
#define BSP_BT_SPP_CHAR_UUID 0xCDEF
// CCCD UUID: 0x2902
#define BSP_BT_SPP_CCCD_UUID 0x2902

#define BSP_BT_SPP_SERVER_ID 1     // SPP 服务 ID
#define BSP_BT_SPP_MTU_SIZE 247    // SPP MTU 大小
#define BSP_BT_SPP_BUFFER_SIZE 244 // SPP 发送缓冲区大小
#define NAME_MAX_LENGTH 20         // 设备名最大长度

// BLE 广播相关定义
#define BLE_ADV_FLAG_LEN 0x03                  // 广播标志长度
#define BLE_ADV_FLAG_DATA 0x05                 // 广播标志数据
#define BLE_ADV_APPEARANCE_LENGTH 4            // 外观字段长度
#define BLE_ADV_APPEARANCE_DATA_TYPE 0x19      // 外观数据类型
#define BLE_ADV_CATEGORY_KEYBOARD_VALUE 0x0080 // 键盘外观值
#define BLE_ADV_PARAM_DATATYPE_LENGTH 1        // 参数数据类型长度
#define BLE_ADV_LOCAL_NAME_DATA_TYPE 0x09      // 本地名称数据类型
#define BLE_ADV_TX_POWER_LEVEL 0x0A            // TX 功率等级类型
#define BLE_SCAN_RSP_TX_POWER_LEVEL_LEN 0x03   // 扫描响应功率长度
#define EXT_ADV_OR_SCAN_RSP_DATA_LEN 251       // 扩展广播数据最大长度

#define BLE_ADV_MIN_INTERVAL 0x30                       // 最小广播间隔
#define BLE_ADV_MAX_INTERVAL 0x60                       // 最大广播间隔
#define BLE_ADV_TYPE_CONNECTABLE_UNDIRECTED 0x00        // 可连接非定向广播
#define BLE_ADV_FILTER_POLICY_SCAN_ANY_CONNECT_ANY 0x00 // 接受所有扫描和连接
#define BLE_ADV_CHANNEL_MAP_CH_DEFAULT 0x07             // 默认全信道
#define BLE_PUBLIC_DEVICE_ADDRESS 0x00                  // 公共设备地址类型
#define BTH_GAP_BLE_ADV_HANDLE_DEFAULT 0x01             // 默认广播句柄

// ==================== 全局变量 ====================
static uint8_t g_server_id = BSP_BT_SPP_SERVER_ID; // BLE SPP 服务器 ID
static volatile uint16_t g_conn_hdl = 0;           // 连接句柄
static uint16_t g_char_handle = 0;                 // 特征值句柄 - 用于接收数据
static uint16_t g_cccd_handle = 0;                 // CCCD句柄 - 用于检测通知开启
static volatile bool g_notify_enabled = false;     // 手机是否订阅了通知

// 共享状态互斥锁：保护 conn_hdl / status / notify_enabled / remote_addr
static osal_mutex g_state_lock;          // 连接状态并发保护锁
static bool g_state_lock_inited = false; // 互斥锁初始化标志

// 重新广播 worker：断连回调里 sem_up，worker 任务 sleep 100ms 后调用 start_adv
// 避免在协议栈回调上下文里调用 msleep 引发协议栈阻塞
static osal_semaphore g_restart_adv_sem;  // 断连后重启广播通知信号量
static bool g_restart_adv_inited = false; // restart 信号量初始化标志
static osal_task *g_adv_worker_task = NULL;
static volatile bool g_adv_worker_run = false;
static uint8_t g_device_name[NAME_MAX_LENGTH] = {'W', 'S', '6', '3', '_', 'U', 'A', 'R', 'T'}; // 蓝牙设备名称

// 蓝牙设备地址
static bd_addr_t g_bt_spp_addr = {
    .type = 0,
    .addr = ROBOT_LOCAL_MAC,
};

// 连接的远程设备地址
static bd_addr_t g_remote_addr = {0};    // 远程设备蓝牙地址
static bool g_remote_addr_valid = false; // 远程地址是否有效

// 回调函数
static bsp_bt_spp_data_handler_t g_data_handler = NULL;              // 数据接收回调函数
static bsp_bt_spp_event_handler_t g_event_handler = NULL;            // 事件回调函数
static bsp_bt_spp_status_t g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE; // 当前连接状态

// 服务器应用UUID
static char g_app_uuid[] = {0x0, 0x0}; // 应用 UUID（由协议栈填充）

// ==================== 辅助结构定义 ====================
// BLE 广播标志结构
typedef struct {
    uint8_t length;        // 数据长度
    uint8_t adv_data_type; // 广播数据类型
    uint8_t flags;         // 标志位
} ble_adv_flag;

// BLE 外观结构
typedef struct {
    uint8_t length;         // 数据长度
    uint8_t adv_data_type;  // 广播数据类型
    uint8_t catogory_id[2]; // 类别 ID
} ble_appearance_st;

// BLE 发送功率等级结构
typedef struct {
    uint8_t length;         // 数据长度
    uint8_t adv_data_type;  // 广播数据类型
    uint8_t tx_power_value; // 发送功率值
} ble_tx_power_level_st;

// ==================== 工具函数 ====================

/**
 * @brief 填充16位UUID
 */
static void fill_uuid16(bt_uuid_t *uuid, uint16_t val)
{
    uuid->uuid_len = UUID_LEN_2;
    uuid->uuid[0] = (uint8_t)(val >> 8);
    uuid->uuid[1] = (uint8_t)val;
}

/**
 * @brief 比较服务UUID
 */
static errcode_t compare_service_uuid(bt_uuid_t *uuid1, bt_uuid_t *uuid2)
{
    if (uuid1->uuid_len != uuid2->uuid_len) {
        return ERRCODE_BT_FAIL;
    }
    if (memcmp(uuid1->uuid, uuid2->uuid, uuid1->uuid_len) != 0) {
        return ERRCODE_BT_FAIL;
    }
    return ERRCODE_BT_SUCCESS;
}

// ==================== 广播配置函数 ====================

/* 取 16 位值的低字节 */
static uint8_t u16_low_u8(uint16_t val)
{
    return (uint8_t)(val & 0xff);
}

/* 取 16 位值的高字节 */
static uint8_t u16_high_u8(uint16_t val)
{
    return (uint8_t)((val >> 8) & 0xff);
}

/* 填充 BLE 广播标志数据（可发现+不支持经典蓝牙） */
static uint8_t ble_set_adv_flag_data(uint8_t *set_adv_data_position, uint8_t max_len)
{
    ble_adv_flag adv_flags = {
        .length = BLE_ADV_FLAG_LEN - 1,
        .adv_data_type = 1,
        .flags = BLE_ADV_FLAG_DATA,
    };
    if (memcpy_s(set_adv_data_position, max_len, &adv_flags, BLE_ADV_FLAG_LEN) != EOK) {
        return 0;
    }
    return BLE_ADV_FLAG_LEN;
}

/* 填充 BLE 广播外观数据（键盘类别） */
static uint8_t ble_set_adv_appearance(uint8_t *set_adv_data_position, uint8_t max_len)
{
    ble_appearance_st adv_appearance_data = {
        .length = BLE_ADV_APPEARANCE_LENGTH - 1,
        .adv_data_type = BLE_ADV_APPEARANCE_DATA_TYPE,
        .catogory_id = {u16_low_u8(BLE_ADV_CATEGORY_KEYBOARD_VALUE), u16_high_u8(BLE_ADV_CATEGORY_KEYBOARD_VALUE)},
    };
    if (memcpy_s(set_adv_data_position, max_len, &adv_appearance_data, BLE_ADV_APPEARANCE_LENGTH) != EOK) {
        return 0;
    }
    return BLE_ADV_APPEARANCE_LENGTH;
}

/* 组装 BLE 广播数据（标志+外观） */
static uint16_t bsp_bt_spp_set_adv_data(uint8_t *set_adv_data, uint8_t adv_data_max_len)
{
    uint8_t idx = 0;

    if ((set_adv_data == NULL) || (adv_data_max_len == 0)) {
        return 0;
    }
    idx += ble_set_adv_flag_data(set_adv_data, adv_data_max_len);
    idx += ble_set_adv_appearance(&set_adv_data[idx], adv_data_max_len - idx);
    return idx;
}

/* 组装 BLE 扫描响应数据（发射功率+设备名称） */
static uint16_t ble_set_scan_response_data(uint8_t *scan_rsp_data, uint8_t scan_rsp_data_max_len)
{
    uint8_t idx = 0;

    if (scan_rsp_data == NULL || scan_rsp_data_max_len == 0) {
        return 0;
    }

    // 发送功率等级
    ble_tx_power_level_st tx_power_level = {
        .length = BLE_SCAN_RSP_TX_POWER_LEVEL_LEN - 1,
        .adv_data_type = BLE_ADV_TX_POWER_LEVEL,
        .tx_power_value = 0,
    };

    if (memcpy_s(scan_rsp_data, scan_rsp_data_max_len, &tx_power_level, sizeof(ble_tx_power_level_st)) != EOK) {
        return 0;
    }
    idx += BLE_SCAN_RSP_TX_POWER_LEVEL_LEN;

    // 设置本地设备名称
    scan_rsp_data[idx++] = (uint8_t)(sizeof(g_device_name) + 1);
    scan_rsp_data[idx++] = BLE_ADV_LOCAL_NAME_DATA_TYPE;
    if ((idx + sizeof(g_device_name)) > scan_rsp_data_max_len) {
        return 0;
    }
    if (memcpy_s(&scan_rsp_data[idx], scan_rsp_data_max_len - idx, g_device_name, sizeof(g_device_name)) != EOK) {
        return 0;
    }
    idx += sizeof(g_device_name);
    return idx;
}

/* 配置并下发 BLE 广播数据和扫描响应数据 */
static uint8_t bsp_bt_spp_config_adv(void)
{
    errcode_t ret;
    uint16_t adv_data_len;
    uint16_t scan_rsp_data_len;
    uint8_t set_adv_data[EXT_ADV_OR_SCAN_RSP_DATA_LEN] = {0};
    uint8_t set_scan_rsp_data[EXT_ADV_OR_SCAN_RSP_DATA_LEN] = {0};
    gap_ble_config_adv_data_t cfg_adv_data;

    adv_data_len = bsp_bt_spp_set_adv_data(set_adv_data, EXT_ADV_OR_SCAN_RSP_DATA_LEN);
    if ((adv_data_len > EXT_ADV_OR_SCAN_RSP_DATA_LEN) || (adv_data_len == 0)) {
        return 1;
    }

    scan_rsp_data_len = ble_set_scan_response_data(set_scan_rsp_data, EXT_ADV_OR_SCAN_RSP_DATA_LEN);
    if ((scan_rsp_data_len > EXT_ADV_OR_SCAN_RSP_DATA_LEN) || (scan_rsp_data_len == 0)) {
        return 1;
    }

    cfg_adv_data.adv_data = set_adv_data;
    cfg_adv_data.adv_length = adv_data_len;
    cfg_adv_data.scan_rsp_data = set_scan_rsp_data;
    cfg_adv_data.scan_rsp_length = scan_rsp_data_len;

    ret = gap_ble_set_adv_data(BTH_GAP_BLE_ADV_HANDLE_DEFAULT, &cfg_adv_data);
    if (ret != 0) {
        printf("BSP BT SPP: Set adv data failed, ret=%d\r\n", ret);
        return 1;
    }
    return 0;
}

/* 设置广播参数并启动 BLE 广播 */
static uint8_t bsp_bt_spp_start_adv(void)
{
    errcode_t ret;
    gap_ble_adv_params_t adv_para = {0};
    int adv_id = BTH_GAP_BLE_ADV_HANDLE_DEFAULT;

    adv_para.min_interval = BLE_ADV_MIN_INTERVAL;
    adv_para.max_interval = BLE_ADV_MAX_INTERVAL;
    adv_para.duration = 0; // 永久广播
    adv_para.peer_addr.type = BLE_PUBLIC_DEVICE_ADDRESS;
    adv_para.channel_map = BLE_ADV_CHANNEL_MAP_CH_DEFAULT;
    adv_para.adv_type = BLE_ADV_TYPE_CONNECTABLE_UNDIRECTED;
    adv_para.adv_filter_policy = BLE_ADV_FILTER_POLICY_SCAN_ANY_CONNECT_ANY;
    (void)memset_s(&adv_para.peer_addr.addr, BD_ADDR_LEN, 0, BD_ADDR_LEN);

    ret = gap_ble_set_adv_param(adv_id, &adv_para);
    if (ret != 0) {
        printf("BSP BT SPP: Set adv param failed, ret=%d\r\n", ret);
        return 1;
    }

    ret = gap_ble_start_adv(adv_id);
    if (ret != 0) {
        printf("BSP BT SPP: Start adv failed, ret=%d\r\n", ret);
        return 1;
    }

    printf("BSP BT SPP: Advertising started\r\n");
    return 0;
}

// ==================== 重启广播 Worker ====================
// 协议栈回调里不能 msleep，否则会阻塞协议栈线程。
// 这里用独立任务承接断连事件，sleep 后再调 start_adv。
/* 重启广播 worker：断连后延时再重启广播，避免阻塞协议栈回调 */
static int bsp_bt_spp_adv_worker(void *arg)
{
    (void)arg;
    while (g_adv_worker_run) {
        if (osal_sem_down(&g_restart_adv_sem) != OSAL_SUCCESS) {
            continue;
        }
        if (!g_adv_worker_run)
            break;
        osal_msleep(100);
        bsp_bt_spp_start_adv();
    }
    return 0;
}

/* 启动重启广播 worker 任务（仅首次创建） */
static void bsp_bt_spp_adv_worker_start(void)
{
    if (!g_restart_adv_inited) {
        osal_sem_binary_sem_init(&g_restart_adv_sem, 0);
        g_restart_adv_inited = true;
    }
    if (g_adv_worker_task != NULL)
        return;
    g_adv_worker_run = true;
    osal_kthread_lock();
    g_adv_worker_task = osal_kthread_create((osal_kthread_handler)bsp_bt_spp_adv_worker, NULL, "bt_adv_w", 2048);
    if (g_adv_worker_task)
        osal_kthread_set_priority(g_adv_worker_task, 25);
    osal_kthread_unlock();
}

// ==================== GATT服务函数 ====================

/**
 * @brief 添加CCCD描述符
 */
static void bsp_bt_spp_add_ccc_descriptor(uint32_t server_id, uint32_t srvc_handle)
{
    bt_uuid_t ccc_uuid = {0};
    uint8_t ccc_data_val[] = {0x00, 0x00};

    fill_uuid16(&ccc_uuid, BSP_BT_SPP_CCCD_UUID);

    gatts_add_desc_info_t descriptor;
    descriptor.desc_uuid = ccc_uuid;
    descriptor.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    descriptor.value_len = sizeof(ccc_data_val);
    descriptor.value = ccc_data_val;
    gatts_add_descriptor(server_id, srvc_handle, &descriptor);
}

/**
 * @brief 添加特征和CCCD
 */
static void bsp_bt_spp_add_characteristic(uint32_t server_id, uint32_t srvc_handle)
{
    bt_uuid_t char_uuid = {0};
    uint8_t char_value[] = {0x00};

    fill_uuid16(&char_uuid, BSP_BT_SPP_CHAR_UUID);

    gatts_add_chara_info_t character;
    character.chara_uuid = char_uuid;
    character.properties =
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE | GATT_CHARACTER_PROPERTY_BIT_NOTIFY;
    character.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    character.value_len = sizeof(char_value);
    character.value = char_value;
    gatts_add_characteristic(server_id, srvc_handle, &character);

    // 添加CCCD描述符
    bsp_bt_spp_add_ccc_descriptor(server_id, srvc_handle);
}

// ==================== 回调函数 ====================

/**
 * @brief 服务添加回调
 */
static void bsp_bt_spp_service_add_cbk(uint8_t server_id, bt_uuid_t *uuid, uint16_t handle, errcode_t status)
{
    bt_uuid_t service_uuid = {0};
    UNUSED(status);

    fill_uuid16(&service_uuid, BSP_BT_SPP_SERVICE_UUID);
    if (compare_service_uuid(uuid, &service_uuid) == ERRCODE_BT_SUCCESS) {
        bsp_bt_spp_add_characteristic(server_id, handle);
        gatts_start_service(server_id, handle);
    }
}

/**
 * @brief 特征添加回调 - 记录特征句柄
 */
static void bsp_bt_spp_char_add_cbk(uint8_t server_id,
                                    bt_uuid_t *uuid,
                                    uint16_t service_handle,
                                    gatts_add_character_result_t *result,
                                    errcode_t status)
{
    UNUSED(server_id);
    UNUSED(uuid);
    UNUSED(service_handle);
    UNUSED(status);

    // 记录特征值句柄
    g_char_handle = result->value_handle;
}

/**
 * @brief 描述符添加回调 - 记录CCCD句柄
 */
static void bsp_bt_spp_desc_add_cbk(uint8_t server_id,
                                    bt_uuid_t *uuid,
                                    uint16_t service_handle,
                                    uint16_t handle,
                                    errcode_t status)
{
    UNUSED(server_id);
    UNUSED(uuid);
    UNUSED(service_handle);
    UNUSED(status);

    // 记录CCCD句柄
    g_cccd_handle = handle;
}

/**
 * @brief 服务启动回调
 */
static void bsp_bt_spp_service_start_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    UNUSED(server_id);
    UNUSED(handle);
    UNUSED(status);
}

/**
 * @brief 写请求回调 (核心数据接收)
 * 处理两种情况：
 * 1. 手机向特征值写入数据 (RX数据)
 * 2. 手机向CCCD写入配置 (开启/关闭通知)
 */
static void bsp_bt_spp_write_req_cbk(uint8_t server_id, uint16_t conn_id, gatts_req_write_cb_t *req, errcode_t status)
{
    UNUSED(server_id);
    UNUSED(conn_id);
    UNUSED(status);

    // 情况1: 手机向特征值写入数据 (这就是我们要的 RX 数据)
    if (req->handle == g_char_handle) {
        // 调用数据接收回调（避免在协议栈回调里做 per-byte printf 刷屏）
        if (g_data_handler != NULL && req->value != NULL && req->length > 0) {
            g_data_handler(req->value, req->length);
        }
        return;
    }

    // 情况2: 手机向CCCD写入配置 (开启/关闭通知)
    if (req->handle == g_cccd_handle) {
        if (req->length == 2) {
            uint16_t ccc_val = req->value[0] | (req->value[1] << 8);
            bool en = (ccc_val & 0x0001) ? true : false;
            if (g_state_lock_inited)
                osal_mutex_lock(&g_state_lock);
            g_notify_enabled = en;
            if (g_state_lock_inited)
                osal_mutex_unlock(&g_state_lock);
        }
        return;
    }

    // 其他句柄的写入，忽略
}

/**
 * @brief 读请求回调
 */
static void bsp_bt_spp_read_req_cbk(uint8_t server_id, uint16_t conn_id, gatts_req_read_cb_t *req, errcode_t status)
{
    UNUSED(server_id);
    UNUSED(conn_id);
    UNUSED(req);
    UNUSED(status);
}

/**
 * @brief MTU改变回调
 */
static void bsp_bt_spp_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id, uint16_t mtu_size, errcode_t status)
{
    UNUSED(server_id);
    UNUSED(conn_id);
    UNUSED(mtu_size);
    UNUSED(status);
}

/**
 * @brief 广播使能回调
 */
static void bsp_bt_spp_adv_enable_cbk(uint8_t adv_id, adv_status_t status)
{
    UNUSED(adv_id);
    UNUSED(status);
}

/**
 * @brief 广播禁用回调
 */
static void bsp_bt_spp_adv_disable_cbk(uint8_t adv_id, adv_status_t status)
{
    UNUSED(adv_id);
    UNUSED(status);
}

/**
 * @brief 连接状态改变回调
 */
static void bsp_bt_spp_connect_change_cbk(uint16_t conn_id,
                                          bd_addr_t *addr,
                                          gap_ble_conn_state_t conn_state,
                                          gap_ble_pair_state_t pair_state,
                                          gap_ble_disc_reason_t disc_reason)
{
    UNUSED(pair_state);
    UNUSED(disc_reason);

    if (conn_state == GAP_BLE_STATE_CONNECTED) {
        if (g_state_lock_inited)
            osal_mutex_lock(&g_state_lock);
        g_conn_hdl = conn_id;
        if (addr != NULL) {
            memcpy_s(&g_remote_addr, sizeof(g_remote_addr), addr, sizeof(bd_addr_t));
            g_remote_addr_valid = true;
        }
        g_bt_spp_status = BSP_BT_SPP_STATUS_CONNECTED;
        g_notify_enabled = false;
        if (g_state_lock_inited)
            osal_mutex_unlock(&g_state_lock);

        printf("[BT] 已连接\r\n");

        // 交换MTU
        gattc_exchange_mtu_req(g_server_id, conn_id, BSP_BT_SPP_MTU_SIZE);

        // 触发连接事件
        if (g_event_handler != NULL) {
            g_event_handler(BSP_BT_SPP_EVENT_CONNECTED, NULL);
        }

    } else if (conn_state == GAP_BLE_STATE_DISCONNECTED) {
        if (g_state_lock_inited)
            osal_mutex_lock(&g_state_lock);
        g_bt_spp_status = BSP_BT_SPP_STATUS_DISCONNECTED;
        g_conn_hdl = 0;
        g_remote_addr_valid = false;
        g_notify_enabled = false;
        if (g_state_lock_inited)
            osal_mutex_unlock(&g_state_lock);

        printf("[BT] 已断开\r\n");

        // 触发断开事件
        if (g_event_handler != NULL) {
            g_event_handler(BSP_BT_SPP_EVENT_DISCONNECTED, NULL);
        }

        // 通知 worker 重新广播（不在协议栈回调里 sleep+start_adv）
        if (g_restart_adv_inited) {
            osal_sem_up(&g_restart_adv_sem);
        }
    }
}

/**
 * @brief 配对结果回调
 */
static void bsp_bt_spp_pair_result_cbk(uint16_t conn_id, const bd_addr_t *addr, errcode_t status)
{
    UNUSED(conn_id);
    UNUSED(addr);
    UNUSED(status);
}

/**
 * @brief 连接参数更新回调
 */
static void bsp_bt_spp_conn_param_update_cbk(uint16_t conn_id,
                                             errcode_t status,
                                             const gap_ble_conn_param_update_t *param)
{
    UNUSED(conn_id);
    UNUSED(status);
    UNUSED(param);
}

/**
 * @brief 注册回调函数
 */
static errcode_t bsp_bt_spp_register_callbacks(void)
{
    errcode_t ret = ERRCODE_BT_SUCCESS;

    // 注册GAP回调
    gap_ble_callbacks_t gap_cb = {0};
    gap_cb.start_adv_cb = bsp_bt_spp_adv_enable_cbk;
    gap_cb.stop_adv_cb = bsp_bt_spp_adv_disable_cbk;
    gap_cb.conn_state_change_cb = bsp_bt_spp_connect_change_cbk;
    gap_cb.pair_result_cb = bsp_bt_spp_pair_result_cbk;
    gap_cb.conn_param_update_cb = bsp_bt_spp_conn_param_update_cbk;
    ret = gap_ble_register_callbacks(&gap_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Reg GAP callbacks failed\r\n");
        return ret;
    }

    // 注册GATTS回调
    gatts_callbacks_t gatt_cb = {0};
    gatt_cb.add_service_cb = bsp_bt_spp_service_add_cbk;
    gatt_cb.add_characteristic_cb = bsp_bt_spp_char_add_cbk;
    gatt_cb.add_descriptor_cb = bsp_bt_spp_desc_add_cbk;
    gatt_cb.start_service_cb = bsp_bt_spp_service_start_cbk;
    gatt_cb.read_request_cb = bsp_bt_spp_read_req_cbk;
    gatt_cb.write_request_cb = bsp_bt_spp_write_req_cbk;
    gatt_cb.mtu_changed_cb = bsp_bt_spp_mtu_changed_cbk;
    ret = gatts_register_callbacks(&gatt_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Reg GATTS callbacks failed\r\n");
        return ret;
    }

    printf("BSP BT SPP: Callbacks registered\r\n");
    return ret;
}

/**
 * @brief 添加服务
 */
static void bsp_bt_spp_add_service(void)
{
    bt_uuid_t service_uuid = {0};
    printf("BSP BT SPP: Adding service...\r\n");
    fill_uuid16(&service_uuid, BSP_BT_SPP_SERVICE_UUID);
    gatts_add_service(BSP_BT_SPP_SERVER_ID, &service_uuid, true);
}

/**
 * @brief 注册GATT服务器
 */
static errcode_t bsp_bt_spp_register_server(void)
{
    bt_uuid_t uuid = {0};
    uuid.uuid_len = sizeof(g_app_uuid);
    if (memcpy_s(uuid.uuid, uuid.uuid_len, g_app_uuid, sizeof(g_app_uuid)) != EOK) {
        return ERRCODE_BT_FAIL;
    }
    return gatts_register_server(&uuid, &g_server_id);
}

// ==================== 对外API函数 ====================

/**
 * @brief 初始化蓝牙SPP
 * @param device_name 蓝牙设备名称
 * @return 0成功，-1失败
 */
int bsp_bt_spp_init(const char *device_name)
{
    errcode_t ret;

    if (device_name == NULL) {
        printf("BSP BT SPP: Invalid device name\r\n");
        return -1;
    }

    printf("BSP BT SPP: Initializing with name '%s'...\r\n", device_name);

    // 状态锁与 worker 提前准备好
    if (!g_state_lock_inited) {
        if (osal_mutex_init(&g_state_lock) == OSAL_SUCCESS) {
            g_state_lock_inited = true;
        }
    }
    bsp_bt_spp_adv_worker_start();

    // 更新设备名称
    uint8_t name_len = strlen(device_name);
    if (name_len > NAME_MAX_LENGTH - 1) {
        name_len = NAME_MAX_LENGTH - 1;
    }
    memcpy_s(g_device_name, NAME_MAX_LENGTH, device_name, name_len);
    g_device_name[name_len] = '\0';

    // 延时等待BLE初始化
    osal_msleep(1000);

    // 使能BLE
    enable_ble();
    printf("BSP BT SPP: BLE enabled\r\n");

    // 注册回调
    ret = bsp_bt_spp_register_callbacks();
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Register callbacks failed\r\n");
        return -1;
    }

    // 注册服务器
    ret = bsp_bt_spp_register_server();
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Register server failed\r\n");
        return -1;
    }

    // 添加服务
    bsp_bt_spp_add_service();

    // 设置本地地址
    gap_ble_set_local_addr(&g_bt_spp_addr);

    // 配置广播数据
    if (bsp_bt_spp_config_adv() != 0) {
        printf("BSP BT SPP: Config adv failed\r\n");
        return -1;
    }

    // 开始广播
    if (bsp_bt_spp_start_adv() != 0) {
        printf("BSP BT SPP: Start adv failed\r\n");
        return -1;
    }

    printf("BSP BT SPP: Init OK. Waiting for connection...\r\n");
    return 0;
}

/**
 * @brief 发送数据
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 实际发送的长度，-1表示失败，-2表示未启用Notify
 */
int bsp_bt_spp_send(const uint8_t *data, uint32_t len)
{
    errcode_t ret;
    gatts_ntf_ind_t param = {0};

    if (data == NULL || len == 0) {
        return -1;
    }

    // 在锁内拿状态快照，避免回调线程在 send 中途清掉句柄
    bsp_bt_spp_status_t st_snap;
    bool remote_valid_snap;
    bool notify_snap;
    uint16_t conn_hdl_snap;
    uint16_t char_handle_snap;
    if (g_state_lock_inited)
        osal_mutex_lock(&g_state_lock);
    st_snap = g_bt_spp_status;
    remote_valid_snap = g_remote_addr_valid;
    notify_snap = g_notify_enabled;
    conn_hdl_snap = g_conn_hdl;
    char_handle_snap = g_char_handle;
    if (g_state_lock_inited)
        osal_mutex_unlock(&g_state_lock);

    if (st_snap != BSP_BT_SPP_STATUS_CONNECTED || !remote_valid_snap) {
        return -1;
    }
    if (char_handle_snap == 0) {
        return -1;
    }
    if (!notify_snap) {
        // 静默失败，不打印日志避免刷屏
        return -2;
    }

    // 限制数据长度
    if (len > BSP_BT_SPP_BUFFER_SIZE) {
        len = BSP_BT_SPP_BUFFER_SIZE;
    }

    // 244 字节栈缓冲，避免每包 vmalloc/vfree 抖动
    uint8_t buffer[BSP_BT_SPP_BUFFER_SIZE];
    memcpy_s(buffer, sizeof(buffer), data, len);

    param.attr_handle = char_handle_snap;
    param.value = buffer;
    param.value_len = len;

    // 发送通知
    ret = gatts_notify_indicate(BSP_BT_SPP_SERVER_ID, conn_hdl_snap, &param);

    if (ret == ERRCODE_BT_SUCCESS) {
        return (int)len;
    }

    printf("[TX] Send failed, ret=%d\r\n", ret);
    return -1;
}

/**
 * @brief 获取SPP连接状态
 * @return SPP状态
 */
bsp_bt_spp_status_t bsp_bt_spp_get_status(void)
{
    return g_bt_spp_status;
}

/**
 * @brief 注册数据接收回调
 * @param handler 数据接收处理函数
 * @return 0成功
 */
int bsp_bt_spp_register_data_handler(bsp_bt_spp_data_handler_t handler)
{
    g_data_handler = handler;
    return 0;
}

/**
 * @brief 注册事件回调
 * @param handler 事件处理函数
 * @return 0成功
 */
int bsp_bt_spp_register_event_handler(bsp_bt_spp_event_handler_t handler)
{
    g_event_handler = handler;
    return 0;
}
