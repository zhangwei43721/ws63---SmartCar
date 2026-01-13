/**
 ****************************************************************************************************
 * @file        bsp_bt_spp.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-13
 * @brief       蓝牙SPP BSP层实现 (基于BLE GATT透传)
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
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

#define BSP_BT_SPP_SERVER_ID 1
#define BSP_BT_SPP_SERVICE_UUID 0xABCD
#define BSP_BT_SPP_CHAR_UUID 0xCDEF
#define BSP_BT_SPP_CLIENT_CHAR_CFG_UUID 0x2902

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
static uint16_t g_notification_char_hdl = 0;
static bsp_bt_spp_data_handler_t g_data_handler = NULL;
static bsp_bt_spp_event_handler_t g_event_handler = NULL;
static bsp_bt_spp_status_t g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE;
static uint8_t g_device_name[NAME_MAX_LENGTH] = {'S', 'm', 'a', 'r', 't', 'C', 'a', 'r', '_', 'B', 'T'};

/* 蓝牙设备地址 */
static bd_addr_t g_bt_spp_addr = {
    .type = 0,
    .addr = {0x11, 0x22, 0x33, 0x63, 0x88, 0x99},
};

/* 连接的远程设备地址 */
static bd_addr_t g_remote_addr = {0};
static bool g_remote_addr_valid = false;

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

/* ==================== 内部函数 ==================== */

/**
 * @brief 触发SPP事件回调
 */
static void bsp_bt_spp_trigger_event(bsp_bt_spp_event_t event, void *data)
{
    if (g_event_handler != NULL) {
        g_event_handler(event, data);
    }
}

/**
 * @brief 将uint16的uuid数字转化为bt_uuid_t
 */
static void stream_data_to_uuid(uint16_t uuid_data, bt_uuid_t *out_uuid)
{
    char uuid[] = {(uint8_t)(uuid_data >> OCTET_BIT_LEN), (uint8_t)uuid_data};
    out_uuid->uuid_len = UUID_LEN_2;
    if (memcpy_s(out_uuid->uuid, out_uuid->uuid_len, uuid, UUID_LEN_2) != EOK) {
        return;
    }
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
 * @brief 添加描述符：客户端特性配置
 */
static void bsp_bt_spp_add_descriptor_ccc(uint32_t server_id, uint32_t srvc_handle)
{
    bt_uuid_t ccc_uuid = {0};
    uint8_t ccc_data_val[] = {0x00, 0x00};

    printf("BSP BT SPP: Adding descriptor CCCD\r\n");
    stream_data_to_uuid(BSP_BT_SPP_CLIENT_CHAR_CFG_UUID, &ccc_uuid);
    gatts_add_desc_info_t descriptor;
    descriptor.desc_uuid = ccc_uuid;
    descriptor.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    descriptor.value_len = sizeof(ccc_data_val);
    descriptor.value = ccc_data_val;
    gatts_add_descriptor(server_id, srvc_handle, &descriptor);
    osal_vfree(ccc_uuid.uuid);
}

/**
 * @brief 添加特征和描述符
 */
static void bsp_bt_spp_add_characters_and_descriptors(uint32_t server_id, uint32_t srvc_handle)
{
    bt_uuid_t char_uuid = {0};
    uint8_t char_value[] = {0x00};

    printf("BSP BT SPP: Adding characteristic\r\n");
    stream_data_to_uuid(BSP_BT_SPP_CHAR_UUID, &char_uuid);

    gatts_add_chara_info_t character;
    character.chara_uuid = char_uuid;
    character.properties =
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE | GATT_CHARACTER_PROPERTY_BIT_NOTIFY;
    character.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    character.value_len = sizeof(char_value);
    character.value = char_value;
    gatts_add_characteristic(server_id, srvc_handle, &character);
    bsp_bt_spp_add_descriptor_ccc(server_id, srvc_handle);
}

/**
 * @brief 服务添加回调
 */
static void bsp_bt_spp_service_add_cbk(uint8_t server_id, bt_uuid_t *uuid, uint16_t handle, errcode_t status)
{
    bt_uuid_t service_uuid = {0};
    printf("BSP BT SPP: Service added - server: %d, status: %d, handle: %d\r\n", server_id, status, handle);

    stream_data_to_uuid(BSP_BT_SPP_SERVICE_UUID, &service_uuid);
    if (compare_service_uuid(uuid, &service_uuid) == ERRCODE_BT_SUCCESS) {
        bsp_bt_spp_add_characters_and_descriptors(server_id, handle);
        printf("BSP BT SPP: Starting service\r\n");
        gatts_start_service(server_id, handle);
    } else {
        printf("BSP BT SPP: Unknown service UUID\r\n");
    }
}

/**
 * @brief 特征添加回调
 */
static void bsp_bt_spp_characteristic_add_cbk(uint8_t server_id,
                                              bt_uuid_t *uuid,
                                              uint16_t service_handle,
                                              gatts_add_character_result_t *result,
                                              errcode_t status)
{
    UNUSED(uuid);
    UNUSED(service_handle);
    UNUSED(status);
    printf("BSP BT SPP: Characteristic added - server: %d, status: %d, handle: 0x%x, val_handle: 0x%x\r\n", server_id,
           status, result->handle, result->value_handle);
    g_notification_char_hdl = result->value_handle;
}

/**
 * @brief 描述符添加回调
 */
static void bsp_bt_spp_descriptor_add_cbk(uint8_t server_id,
                                          bt_uuid_t *uuid,
                                          uint16_t service_handle,
                                          uint16_t handle,
                                          errcode_t status)
{
    UNUSED(uuid);
    UNUSED(service_handle);
    printf("BSP BT SPP: Descriptor added - server: %d, status: %d, handle: 0x%x\r\n", server_id, status, handle);
}

/**
 * @brief 服务启动回调
 */
static void bsp_bt_spp_service_start_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    printf("BSP BT SPP: Service started - server: %d, status: %d, handle: %d\r\n", server_id, status, handle);
}

/**
 * @brief 写请求回调（接收数据）
 */
static void bsp_bt_spp_write_req_cbk(uint8_t server_id,
                                     uint16_t conn_id,
                                     gatts_req_write_cb_t *write_cb_para,
                                     errcode_t status)
{
    UNUSED(status);

    /* 只处理写入我们自定义特征的数据 */
    if (write_cb_para->handle == g_notification_char_hdl) {
        printf("BSP BT SPP: RX Data - handle:0x%x, len:%d\r\n", write_cb_para->handle, write_cb_para->length);

        /* 调用数据接收回调 */
        if (g_data_handler != NULL && write_cb_para->value != NULL && write_cb_para->length > 0) {
            g_data_handler(write_cb_para->value, write_cb_para->length);
        }

        /* 打印接收到的数据 */
        printf("BSP BT SPP: ");
        for (uint8_t i = 0; i < write_cb_para->length && i < 32; i++) {
            if (write_cb_para->value[i] >= 32 && write_cb_para->value[i] <= 126) {
                printf("%c", write_cb_para->value[i]);
            } else {
                printf("%02x ", write_cb_para->value[i]);
            }
        }
        printf("\r\n");
        return;
    }

    /* 检查是否是CCCD写入（开启/关闭通知） */
    if (write_cb_para->handle == g_notification_char_hdl + 1) {
        uint16_t ccc_value = 0;
        if (write_cb_para->length >= 2) {
            ccc_value = (uint16_t)((write_cb_para->value[1] << 8) | write_cb_para->value[0]);
        }
        printf("BSP BT SPP: CCCD Write - Notify %s\r\n", (ccc_value & 0x0001) ? "ENABLED" : "DISABLED");
        return;
    }

    /* 其他句柄的写入，忽略 */
    printf("BSP BT SPP: Ignore write to handle 0x%x\r\n", write_cb_para->handle);
}

/**
 * @brief 读请求回调
 */
static void bsp_bt_spp_read_req_cbk(uint8_t server_id,
                                    uint16_t conn_id,
                                    gatts_req_read_cb_t *read_cb_para,
                                    errcode_t status)
{
    UNUSED(status);
    printf("BSP BT SPP: Read - server:%d, conn:%d, handle:%d\r\n", server_id, conn_id, read_cb_para->handle);
}

/**
 * @brief MTU改变回调
 */
static void bsp_bt_spp_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id, uint16_t mtu_size, errcode_t status)
{
    printf("BSP BT SPP: MTU changed - server: %d, conn: %d, mtu: %d, status: %d\r\n", server_id, conn_id, mtu_size,
           status);
}

/**
 * @brief 广播使能回调
 */
static void bsp_bt_spp_adv_enable_cbk(uint8_t adv_id, adv_status_t status)
{
    printf("BSP BT SPP: Adv enabled - id: %d, status: %d\r\n", adv_id, status);
}

/**
 * @brief 广播禁用回调
 */
static void bsp_bt_spp_adv_disable_cbk(uint8_t adv_id, adv_status_t status)
{
    printf("BSP BT SPP: Adv disabled - id: %d, status: %d\r\n", adv_id, status);
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
    printf("BSP BT SPP: Conn state - id:%d, state:%d, pair:%d, reason:%d\r\n", conn_id, conn_state, pair_state,
           disc_reason);
    g_conn_hdl = conn_id;

    if (conn_state == GAP_BLE_STATE_CONNECTED) {
        /* 保存远程设备地址 */
        if (addr != NULL) {
            memcpy_s(&g_remote_addr, sizeof(g_remote_addr), addr, sizeof(bd_addr_t));
            g_remote_addr_valid = true;
        }

        printf("BSP BT SPP: Connected!\r\n");
        g_bt_spp_status = BSP_BT_SPP_STATUS_CONNECTED;

        /* 交换MTU */
        gattc_exchange_mtu_req(g_server_id, conn_id, BSP_BT_SPP_MTU_SIZE);

        /* 更新连接参数 */
        gap_conn_param_update_t conn_param = {0};
        conn_param.conn_handle = conn_id;
        conn_param.interval_min = 0x10; /* 20ms */
        conn_param.interval_max = 0x20; /* 40ms */
        conn_param.slave_latency = 0;
        conn_param.timeout_multiplier = 0x1f4;
        gap_ble_connect_param_update(&conn_param);

        bsp_bt_spp_trigger_event(BSP_BT_SPP_EVENT_CONNECTED, NULL);

    } else if (conn_state == GAP_BLE_STATE_DISCONNECTED) {
        printf("BSP BT SPP: Disconnected!\r\n");
        g_bt_spp_status = BSP_BT_SPP_STATUS_DISCONNECTED;
        g_conn_hdl = 0;
        g_remote_addr_valid = false;
        bsp_bt_spp_trigger_event(BSP_BT_SPP_EVENT_DISCONNECTED, NULL);

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
    UNUSED(addr);
    printf("BSP BT SPP: Pair result - conn: %d, status: %d\r\n", conn_id, status);
}

/**
 * @brief 连接参数更新回调
 */
static void bsp_bt_spp_conn_param_update_cbk(uint16_t conn_id,
                                             errcode_t status,
                                             const gap_ble_conn_param_update_t *param)
{
    printf("BSP BT SPP: Param updated - conn: %d, status: %d, interval: %d\r\n", conn_id, status, param->interval);
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
    gatts_callbacks_t service_cb = {0};
    service_cb.add_service_cb = bsp_bt_spp_service_add_cbk;
    service_cb.add_characteristic_cb = bsp_bt_spp_characteristic_add_cbk;
    service_cb.add_descriptor_cb = bsp_bt_spp_descriptor_add_cbk;
    service_cb.start_service_cb = bsp_bt_spp_service_start_cbk;
    service_cb.read_request_cb = bsp_bt_spp_read_req_cbk;
    service_cb.write_request_cb = bsp_bt_spp_write_req_cbk;
    service_cb.mtu_changed_cb = bsp_bt_spp_mtu_changed_cbk;
    ret = gatts_register_callbacks(&service_cb);
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
static uint8_t bsp_bt_spp_add_service(void)
{
    bt_uuid_t service_uuid = {0};
    printf("BSP BT SPP: Adding service...\r\n");
    stream_data_to_uuid(BSP_BT_SPP_SERVICE_UUID, &service_uuid);
    gatts_add_service(BSP_BT_SPP_SERVER_ID, &service_uuid, true);
    return ERRCODE_BT_SUCCESS;
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

    g_bt_spp_status = BSP_BT_SPP_STATUS_IDLE;
    printf("BSP BT SPP: Initialized successfully\r\n");

    return 0;
}

/**
 * @brief 等待SPP连接
 * @param timeout_ms 超时时间（毫秒），0表示一直等待
 * @return 0成功，-1失败或超时
 */
int bsp_bt_spp_wait_connection(uint32_t timeout_ms)
{
    printf("BSP BT SPP: Waiting for connection...\r\n");

    uint32_t elapsed = 0;
    uint32_t sleep_time = 100;

    while (g_bt_spp_status != BSP_BT_SPP_STATUS_CONNECTED) {
        if (timeout_ms > 0 && elapsed >= timeout_ms) {
            printf("BSP BT SPP: Wait connection timeout\r\n");
            return -1;
        }
        osal_msleep(sleep_time);
        if (timeout_ms > 0) {
            elapsed += sleep_time;
        }
    }

    printf("BSP BT SPP: Connected\r\n");
    return 0;
}

/**
 * @brief 发送数据
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 实际发送的长度，-1表示失败
 */
int bsp_bt_spp_send(const uint8_t *data, uint32_t len)
{
    errcode_t ret;
    gatts_ntf_ind_t param = {0};

    if (data == NULL || len == 0) {
        return -1;
    }

    if (g_bt_spp_status != BSP_BT_SPP_STATUS_CONNECTED || !g_remote_addr_valid) {
        printf("BSP BT SPP: Not connected (status:%d, addr_valid:%d)\r\n", g_bt_spp_status, g_remote_addr_valid);
        return -1;
    }

    if (g_notification_char_hdl == 0) {
        printf("BSP BT SPP: Characteristic handle not set\r\n");
        return -1;
    }

    if (len > BSP_BT_SPP_BUFFER_SIZE) {
        len = BSP_BT_SPP_BUFFER_SIZE;
    }

    printf("BSP BT SPP: Sending %u bytes via handle 0x%x, conn_id=%d\r\n", len, g_notification_char_hdl, g_conn_hdl);

    param.attr_handle = g_notification_char_hdl;
    param.value = osal_vmalloc(len);
    if (param.value == NULL) {
        printf("BSP BT SPP: Alloc failed\r\n");
        return -1;
    }

    param.value_len = len;
    if (memcpy_s(param.value, param.value_len, data, len) != EOK) {
        osal_vfree(param.value);
        return -1;
    }

    ret = gatts_notify_indicate(BSP_BT_SPP_SERVER_ID, g_conn_hdl, &param);
    osal_vfree(param.value);

    if (ret == ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Send OK\r\n");
        return (int)len;
    }

    printf("BSP BT SPP: Send failed, ret=%d\r\n", ret);
    return -1;
}

/**
 * @brief 断开SPP连接
 * @return 0成功，-1失败
 */
int bsp_bt_spp_disconnect(void)
{
    errcode_t ret;

    if (g_bt_spp_status != BSP_BT_SPP_STATUS_CONNECTED || !g_remote_addr_valid) {
        printf("BSP BT SPP: Not connected\r\n");
        return -1;
    }

    printf("BSP BT SPP: Disconnecting...\r\n");

    ret = gap_ble_disconnect_remote_device(&g_remote_addr);
    if (ret != ERRCODE_BT_SUCCESS) {
        printf("BSP BT SPP: Disconnect failed, ret=%d\r\n", ret);
        return -1;
    }

    return 0;
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
 * @return 0成功，-1失败
 */
int bsp_bt_spp_register_data_handler(bsp_bt_spp_data_handler_t handler)
{
    g_data_handler = handler;
    return 0;
}

/**
 * @brief 注册事件回调
 * @param handler 事件处理函数
 * @return 0成功，-1失败
 */
int bsp_bt_spp_register_event_handler(bsp_bt_spp_event_handler_t handler)
{
    g_event_handler = handler;
    return 0;
}
