/**
 ****************************************************************************************************
 * @file        bsp_bt_spp.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-01-13
 * @brief       蓝牙SPP BSP层实现 (基于BLE GATT透传 - 优化版)
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 * 使用说明:
 * 1. 服务UUID: 0xABCD (0000ABCD-0000-1000-8000-00805F9B34FB)
 * 2. 特征UUID: 0xCDEF (0000CDEF-0000-1000-8000-00805F9B34FB) - 支持Read/Write/Notify
 * 3. 客户端需要先写入CCCD启用Notify才能接收数据
 *
 ****************************************************************************************************
 */

#include "bsp_bt_spp.h"
#include "soc_osal.h"
#include "std_def.h"
#include "app_init.h"
#include "systick.h"
#include "osal_addr.h"
#include "bts_def.h"
#include "bts_le_gap.h"
#include "bts_gatt_stru.h"
#include "bts_gatt_server.h"
#include "bts_gatt_client.h"
#include "securec.h"
#include "errcode.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ==================== 常量定义 ==================== */
#define OCTET_BIT_LEN 8
#define UUID_LEN_2 2

/* ==================== 用户UUID配置 ==================== */
/* 服务 UUID: 0000ABCD-... */
#define BSP_BT_SPP_SERVICE_UUID 0xABCD
/* 特征 UUID: 0000CDEF-... (读+写+通知) <-- 操作这个！ */
#define BSP_BT_SPP_CHAR_UUID 0xCDEF
/* CCCD UUID: 0x2902 */
#define BSP_BT_SPP_CCCD_UUID 0x2902

#define BSP_BT_SPP_SERVER_ID 1
#define BSP_BT_SPP_MTU_SIZE 247
#define BSP_BT_SPP_BUFFER_SIZE 244
#define NAME_MAX_LENGTH 20

/* BLE 广播相关定义 */
#define BLE_ADV_FLAG_LEN 0x03
#define BLE_ADV_FLAG_DATA 0x05
#define BLE_ADV_APPEARANCE_LENGTH 4
#define BLE_ADV_APPEARANCE_DATA_TYPE 0x19
#define BLE_ADV_CATEGORY_KEYBOARD_VALUE 0x0080
#define BLE_ADV_PARAM_DATATYPE_LENGTH 1
#define BLE_ADV_LOCAL_NAME_DATA_TYPE 0x09
#define BLE_ADV_TX_POWER_LEVEL 0x0A
#define BLE_SCAN_RSP_TX_POWER_LEVEL_LEN 0x03
#define EXT_ADV_OR_SCAN_RSP_DATA_LEN 251

#define BLE_ADV_MIN_INTERVAL 0x30
#define BLE_ADV_MAX_INTERVAL 0x60
#define BLE_ADV_TYPE_CONNECTABLE_UNDIRECTED 0x00
#define BLE_ADV_FILTER_POLICY_SCAN_ANY_CONNECT_ANY 0x00
#define BLE_ADV_CHANNEL_MAP_CH_DEFAULT 0x07
#define BLE_PUBLIC_DEVICE_ADDRESS 0x00
#define BTH_GAP_BLE_ADV_HANDLE_DEFAULT 0x01

/* ==================== 全局变量 ==================== */
static uint8_t g_server_id = BSP_BT_SPP_SERVER_ID;
static uint16_t g_conn_hdl = 0;
static uint16_t g_char_handle = 0;    /* 特征值句柄 - 用于接收数据 */
static uint16_t g_cccd_handle = 0;    /* CCCD句柄 - 用于检测通知开启 */
static bool g_notify_enabled = false; /* 手机是否订阅了通知 */
static uint8_t g_device_name[NAME_MAX_LENGTH] = {'W', 'S', '6', '3', '_', 'U', 'A', 'R', 'T'};

/* 蓝牙设备地址 */
static bd_addr_t g_bt_spp_addr = {
    .type = 0,
    .addr = {0x11, 0x22, 0x33, 0x63, 0x88, 0x99},
};

/* 连接的远程设备地址 */
static bd_addr_t g_remote_addr = {0};
static bool g_remote_addr_valid = false;

/* 回调函数 */
static bsp_bt_spp_data_handler_t g_data_handler = NULL;
static bsp_bt_spp_event_handler_t g_event_handler = NULL;
static bsp_bt_spp_status_t g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE;

/* server app uuid */
static char g_app_uuid[] = {0x0, 0x0};

/* ==================== 辅助结构定义 ==================== */
typedef struct {
    uint8_t length;
    uint8_t adv_data_type;
    uint8_t flags;
} ble_adv_flag;

typedef struct {
    uint8_t length;
    uint8_t adv_data_type;
    uint8_t catogory_id[2];
} ble_appearance_st;

typedef struct {
    uint8_t length;
    uint8_t adv_data_type;
    uint8_t tx_power_value;
} ble_tx_power_level_st;

/* ==================== 工具函数 ==================== */

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

/* ==================== 广播配置函数 ==================== */

static uint8_t u16_low_u8(uint16_t val)
{
    return (uint8_t)(val & 0xff);
}

static uint8_t u16_high_u8(uint16_t val)
{
    return (uint8_t)((val >> 8) & 0xff);
}

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

static uint16_t ble_set_scan_response_data(uint8_t *scan_rsp_data, uint8_t scan_rsp_data_max_len)
{
    uint8_t idx = 0;

    if (scan_rsp_data == NULL || scan_rsp_data_max_len == 0) {
        return 0;
    }

    /* tx power level */
    ble_tx_power_level_st tx_power_level = {
        .length = BLE_SCAN_RSP_TX_POWER_LEVEL_LEN - 1,
        .adv_data_type = BLE_ADV_TX_POWER_LEVEL,
        .tx_power_value = 0,
    };

    if (memcpy_s(scan_rsp_data, scan_rsp_data_max_len, &tx_power_level, sizeof(ble_tx_power_level_st)) != EOK) {
        return 0;
    }
    idx += BLE_SCAN_RSP_TX_POWER_LEVEL_LEN;

    /* set local name */
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

static uint8_t bsp_bt_spp_start_adv(void)
{
    errcode_t ret;
    gap_ble_adv_params_t adv_para = {0};
    int adv_id = BTH_GAP_BLE_ADV_HANDLE_DEFAULT;

    adv_para.min_interval = BLE_ADV_MIN_INTERVAL;
    adv_para.max_interval = BLE_ADV_MAX_INTERVAL;
    adv_para.duration = 0; /* 永久广播 */
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

/* ==================== GATT服务函数 ==================== */

/**
 * @brief 添加CCCD描述符
 */
static void bsp_bt_spp_add_ccc_descriptor(uint32_t server_id, uint32_t srvc_handle)
{
    bt_uuid_t ccc_uuid = {0};
    uint8_t ccc_data_val[] = {0x00, 0x00};

    printf("BSP BT SPP: Adding CCCD descriptor\r\n");
    fill_uuid16(&ccc_uuid, BSP_BT_SPP_CCCD_UUID);

    gatts_add_desc_info_t descriptor;
    descriptor.desc_uuid = ccc_uuid;
    descriptor.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    descriptor.value_len = sizeof(ccc_data_val);
    descriptor.value = ccc_data_val;
    gatts_add_descriptor(server_id, srvc_handle, &descriptor);
    osal_vfree(ccc_uuid.uuid);
}

/**
 * @brief 添加特征和CCCD
 */
static void bsp_bt_spp_add_characteristic(uint32_t server_id, uint32_t srvc_handle)
{
    bt_uuid_t char_uuid = {0};
    uint8_t char_value[] = {0x00};

    printf("BSP BT SPP: Adding characteristic\r\n");
    fill_uuid16(&char_uuid, BSP_BT_SPP_CHAR_UUID);

    gatts_add_chara_info_t character;
    character.chara_uuid = char_uuid;
    character.properties =
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE | GATT_CHARACTER_PROPERTY_BIT_NOTIFY;
    character.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    character.value_len = sizeof(char_value);
    character.value = char_value;
    gatts_add_characteristic(server_id, srvc_handle, &character);

    /* 添加CCCD描述符 */
    bsp_bt_spp_add_ccc_descriptor(server_id, srvc_handle);
}

/* ==================== 回调函数 ==================== */

/**
 * @brief 服务添加回调
 */
static void bsp_bt_spp_service_add_cbk(uint8_t server_id, bt_uuid_t *uuid, uint16_t handle, errcode_t status)
{
    bt_uuid_t service_uuid = {0};
    printf("BSP BT SPP: Service added - server: %d, status: %d, handle: %d\r\n", server_id, status, handle);

    fill_uuid16(&service_uuid, BSP_BT_SPP_SERVICE_UUID);
    if (compare_service_uuid(uuid, &service_uuid) == ERRCODE_BT_SUCCESS) {
        bsp_bt_spp_add_characteristic(server_id, handle);
        printf("BSP BT SPP: Starting service\r\n");
        gatts_start_service(server_id, handle);
    } else {
        printf("BSP BT SPP: Unknown service UUID\r\n");
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

    /* 记录特征值句柄 */
    g_char_handle = result->value_handle;
    printf("BSP BT SPP: Char added - Value Handle: 0x%x (Target for Write)\r\n", g_char_handle);
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

    /* 记录CCCD句柄 */
    g_cccd_handle = handle;
    printf("BSP BT SPP: CCCD added - Handle: 0x%x\r\n", g_cccd_handle);
}

/**
 * @brief 服务启动回调
 */
static void bsp_bt_spp_service_start_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    UNUSED(server_id);
    UNUSED(handle);
    UNUSED(status);
    printf("BSP BT SPP: Service started\r\n");
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

    /* 情况1: 手机向特征值写入数据 (这就是我们要的 RX 数据) */
    if (req->handle == g_char_handle) {
        printf("[RX] Handle:0x%x, Len:%d\r\n", req->handle, req->length);

        /* 调用数据接收回调 */
        if (g_data_handler != NULL && req->value != NULL && req->length > 0) {
            g_data_handler(req->value, req->length);
        }

        /* 打印接收到的数据 (可打印字符直接显示，其他显示十六进制) */
        printf("[RX] Data: ");
        for (uint16_t i = 0; i < req->length && i < 64; i++) {
            if (req->value[i] >= 32 && req->value[i] <= 126) {
                printf("%c", req->value[i]);
            } else {
                printf("%02X ", req->value[i]);
            }
        }
        printf("\r\n");
        return;
    }

    /* 情况2: 手机向CCCD写入配置 (开启/关闭通知) */
    if (req->handle == g_cccd_handle) {
        if (req->length == 2) {
            uint16_t ccc_val = req->value[0] | (req->value[1] << 8);
            g_notify_enabled = (ccc_val & 0x0001) ? true : false;
            printf("[CCCD] Notify %s\r\n", g_notify_enabled ? "ENABLED" : "DISABLED");
        }
        return;
    }

    /* 其他句柄的写入，忽略 */
    printf("[IGNORE] Write to handle 0x%x\r\n", req->handle);
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
    UNUSED(status);
    printf("BSP BT SPP: MTU changed to %d\r\n", mtu_size);
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

    printf("BSP BT SPP: Conn state - id:%d, state:%d\r\n", conn_id, conn_state);
    g_conn_hdl = conn_id;

    if (conn_state == GAP_BLE_STATE_CONNECTED) {
        /* 保存远程设备地址 */
        if (addr != NULL) {
            memcpy_s(&g_remote_addr, sizeof(g_remote_addr), addr, sizeof(bd_addr_t));
            g_remote_addr_valid = true;
        }

        printf("[EVENT] Connected! Please enable Notify on APP\r\n");
        g_bt_spp_status = BSP_BT_SPP_STATUS_CONNECTED;
        g_notify_enabled = false; /* 重置通知状态 */

        /* 交换MTU */
        gattc_exchange_mtu_req(g_server_id, conn_id, BSP_BT_SPP_MTU_SIZE);

        /* 触发连接事件 */
        if (g_event_handler != NULL) {
            g_event_handler(BSP_BT_SPP_EVENT_CONNECTED, NULL);
        }

    } else if (conn_state == GAP_BLE_STATE_DISCONNECTED) {
        printf("[EVENT] Disconnected!\r\n");
        g_bt_spp_status = BSP_BT_SPP_STATUS_DISCONNECTED;
        g_conn_hdl = 0;
        g_remote_addr_valid = false;
        g_notify_enabled = false;

        /* 触发断开事件 */
        if (g_event_handler != NULL) {
            g_event_handler(BSP_BT_SPP_EVENT_DISCONNECTED, NULL);
        }

        /* 重新开始广播 */
        osal_msleep(100);
        bsp_bt_spp_start_adv();
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

    /* 注册GAP回调 */
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

    /* 注册GATTS回调 */
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

/* ==================== 对外API函数 ==================== */

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

    /* 更新设备名称 */
    uint8_t name_len = strlen(device_name);
    if (name_len > NAME_MAX_LENGTH - 1) {
        name_len = NAME_MAX_LENGTH - 1;
    }
    memcpy_s(g_device_name, NAME_MAX_LENGTH, device_name, name_len);
    g_device_name[name_len] = '\0';

    /* 延时等待BLE初始化 */
    osal_msleep(1000);

    /* 使能BLE */
    enable_ble();
    printf("BSP BT SPP: BLE enabled\r\n");

    /* 注册回调 */
    ret = bsp_bt_spp_register_callbacks();
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Register callbacks failed\r\n");
        return -1;
    }

    /* 注册服务器 */
    ret = bsp_bt_spp_register_server();
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Register server failed\r\n");
        return -1;
    }

    /* 添加服务 */
    bsp_bt_spp_add_service();

    /* 设置本地地址 */
    gap_ble_set_local_addr(&g_bt_spp_addr);

    /* 配置广播数据 */
    if (bsp_bt_spp_config_adv() != 0) {
        printf("BSP BT SPP: Config adv failed\r\n");
        return -1;
    }

    /* 开始广播 */
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
    uint8_t *buffer = NULL;

    if (data == NULL || len == 0) {
        return -1;
    }

    /* 检查连接状态 */
    if (g_bt_spp_status != BSP_BT_SPP_STATUS_CONNECTED || !g_remote_addr_valid) {
        printf("[TX] Not connected\r\n");
        return -1;
    }

    /* 检查特征句柄 */
    if (g_char_handle == 0) {
        printf("[TX] Characteristic handle not set\r\n");
        return -1;
    }

    /* 检查Notify是否启用 */
    if (!g_notify_enabled) {
        /* 静默失败，不打印日志避免刷屏 */
        return -2;
    }

    /* 限制数据长度 */
    if (len > BSP_BT_SPP_BUFFER_SIZE) {
        len = BSP_BT_SPP_BUFFER_SIZE;
    }

    /* 分配内存并复制数据 */
    buffer = osal_vmalloc(len);
    if (buffer == NULL) {
        printf("[TX] Alloc failed\r\n");
        return -1;
    }
    memcpy_s(buffer, len, data, len);

    param.attr_handle = g_char_handle;
    param.value = buffer;
    param.value_len = len;

    printf("[TX] Sending %u bytes via handle 0x%x\r\n", len, g_char_handle);

    /* 发送通知 */
    ret = gatts_notify_indicate(BSP_BT_SPP_SERVER_ID, g_conn_hdl, &param);
    osal_vfree(buffer);

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
