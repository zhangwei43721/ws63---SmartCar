/*
 * Copyright (c) 2024 HiSilicon Technologies CO., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sle_device.h"

#include "../../board_config.h"
#include "common_def.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "sle_transmition_manager.h"
#include "soc_osal.h"

// ==================== 配置参数 ====================

#define SLE_DEVICE_NAME "SmartCar_SLE" // SLE 设备广播名
#define SLE_DEVICE_NAME_MAX_LEN 15     // 设备名最大长度

// UUID 定义（2 字节短 UUID）
#define SLE_UUID_SERVICE 0xA001 // 服务 UUID
#define SLE_UUID_DATA 0xA002    // 数据特征 UUID

// 连接参数
#define SLE_CONN_INTERVAL_MIN 0x10 // 20ms (125us * 0x10)
#define SLE_CONN_INTERVAL_MAX 0x10 // 20ms
#define SLE_CONN_TIMEOUT 0x1F4     // 5000ms (10ms * 0x1F4)
#define SLE_CONN_MAX_LATENCY 0     // 最大延迟(无跳过)

// 广播参数
#define SLE_ADV_INTERVAL_MIN 0xA0 // 20ms
#define SLE_ADV_INTERVAL_MAX 0xA0 // 20ms
#define SLE_ADV_TX_POWER 20       // 20dBm

#define SLE_ADV_DATA_LEN_MAX 251 // 广播数据最大长度
#define SLE_MTU_SIZE 247         // 默认 MTU 大小

// SLE 广播常量定义（从示例代码复制）
#define SLE_ADV_HANDLE_DEFAULT 1 // 默认广播句柄

// 广播信道映射
#define SLE_ADV_CHANNEL_MAP_77 0x01      // 信道 77 位掩码
#define SLE_ADV_CHANNEL_MAP_78 0x02      // 信道 78 位掩码
#define SLE_ADV_CHANNEL_MAP_79 0x04      // 信道 79 位掩码
#define SLE_ADV_CHANNEL_MAP_DEFAULT 0x07 // 默认全信道

// 广播数据类型
#define SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL 0x01     // 发现级别
#define SLE_ADV_DATA_TYPE_ACCESS_MODE 0x02         // 访问模式
#define SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME 0x0B // 完整本地名称
#define SLE_ADV_DATA_TYPE_TX_POWER_LEVEL 0x0C      // 发射功率等级

// SLE 广播数据结构（从示例代码复制）
struct sle_adv_common_value {
    uint8_t length;
    uint8_t type;
    uint8_t value;
};

// ==================== 内部状态 ====================

static uint8_t g_server_id = 0;        // SSAP 服务端 ID
static uint16_t g_service_handle = 0;  // SLE 服务句柄
static uint16_t g_property_handle = 0; // SLE 数据特征句柄
static uint16_t g_conn_id = 0xFFFF;    // 0xFFFF 表示未连接
static volatile bool g_connected = false;

// 共享状态互斥锁
static osal_mutex g_sle_lock;          // 连接状态并发保护锁
static bool g_sle_lock_inited = false; // 互斥锁初始化标志

// 重启广播 worker：协议栈回调不能直接调用 sle_start_announce 后立即处理逻辑
static osal_semaphore g_restart_sem;      // 断连后重启广播通知信号量
static bool g_restart_sem_inited = false; // restart 信号量初始化标志
static osal_task *g_sle_worker = NULL;
static volatile bool g_sle_worker_run = false;

// 回调函数指针
static sle_connect_cb_t g_connect_cb = NULL;
static sle_disconnect_cb_t g_disconnect_cb = NULL;
static sle_data_recv_cb_t g_data_recv_cb = NULL;

// 特征值存储（8 字节）
static uint8_t g_property_value[8] = {0};

// ==================== UUID 辅助函数 ====================

// 设置 SLE UUID 基础模板
static void sle_uuid_set_base(sle_uuid_t *out)
{
    static const uint8_t sle_uuid_base[] = {0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
                                            0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    (void)memcpy_s(out->uuid, SLE_UUID_LEN, sle_uuid_base, SLE_UUID_LEN);
    out->len = 2; // 2 字节 UUID
}

// 基于模板填充 2 字节短 UUID
static void sle_uuid_set_u2(uint16_t u2, sle_uuid_t *out)
{
    sle_uuid_set_base(out);
    out->uuid[14] = (uint8_t)(u2 & 0xFF);
    out->uuid[15] = (uint8_t)((u2 >> 8) & 0xFF);
}

#define encode2byte_little(ptr, data)                     \
    do {                                                  \
        *(uint8_t *)((ptr) + 1) = (uint8_t)((data) >> 8); \
        *(uint8_t *)(ptr) = (uint8_t)(data);              \
    } while (0)

// ==================== SSAP 回调函数 ====================

// SSAP 读请求回调
static void ssaps_read_request_cbk(uint8_t server_id,
                                   uint16_t conn_id,
                                   ssaps_req_read_cb_t *read_cb_para,
                                   errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(read_cb_para);
    unused(status);
    // 读请求：读取当前特征值
}

// SSAP 写请求回调：更新特征值并触发数据接收回调
static void ssaps_write_request_cbk(uint8_t server_id,
                                    uint16_t conn_id,
                                    ssaps_req_write_cb_t *write_cb_para,
                                    errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(status);

    // 调用数据接收回调
    if (g_data_recv_cb != NULL && write_cb_para->value != NULL) {
        g_data_recv_cb(write_cb_para->value, write_cb_para->length);
    }

    // 更新特征值
    if (write_cb_para->length <= sizeof(g_property_value)) {
        (void)memcpy_s(g_property_value, sizeof(g_property_value), write_cb_para->value, write_cb_para->length);
    }
}

// SSAP MTU 变更回调
static void ssaps_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id, ssap_exchange_info_t *mtu_size, errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(mtu_size);
    unused(status);
}

// SSAP 服务启动回调
static void ssaps_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    unused(server_id);
    unused(handle);
    unused(status);
}

// 注册 SSAP 服务端所有回调
static void sle_ssaps_register_cbks(void)
{
    ssaps_callbacks_t ssaps_cbk = {0};
    ssaps_cbk.start_service_cb = ssaps_start_service_cbk;
    ssaps_cbk.mtu_changed_cb = ssaps_mtu_changed_cbk;
    ssaps_cbk.read_request_cb = ssaps_read_request_cbk;
    ssaps_cbk.write_request_cb = ssaps_write_request_cbk;
    ssaps_register_callbacks(&ssaps_cbk);
}

// ==================== 连接管理回调 ====================

// SLE 连接状态变化回调：更新连接状态，断连时通知 worker 重新广播
static void sle_connect_state_changed_cbk(uint16_t conn_id,
                                          const sle_addr_t *addr,
                                          sle_acb_state_t conn_state,
                                          sle_pair_state_t pair_state,
                                          sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);
    unused(disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (g_sle_lock_inited)
            osal_mutex_lock(&g_sle_lock);
        g_conn_id = conn_id;
        g_connected = true;
        if (g_sle_lock_inited)
            osal_mutex_unlock(&g_sle_lock);

        printf("[SLE] 已连接\r\n");

        // 更新连接参数
        sle_connection_param_update_t param = {0};
        param.conn_id = conn_id;
        param.interval_min = SLE_CONN_INTERVAL_MIN;
        param.interval_max = SLE_CONN_INTERVAL_MAX;
        param.max_latency = SLE_CONN_MAX_LATENCY;
        param.supervision_timeout = SLE_CONN_TIMEOUT;
        sle_update_connect_param(&param);

        // 调用连接回调
        if (g_connect_cb != NULL) {
            g_connect_cb(conn_id);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        if (g_sle_lock_inited)
            osal_mutex_lock(&g_sle_lock);
        g_conn_id = 0xFFFF;
        g_connected = false;
        if (g_sle_lock_inited)
            osal_mutex_unlock(&g_sle_lock);

        printf("[SLE] 已断开\r\n");

        // 调用断开连接回调
        if (g_disconnect_cb != NULL) {
            g_disconnect_cb(conn_id);
        }

        // 通知 worker 重新广播，避免在协议栈上下文里调用栈接口
        if (g_restart_sem_inited) {
            osal_sem_up(&g_restart_sem);
        }
    }
}

// SLE 配对完成回调
static void sle_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    unused(conn_id);
    unused(addr);
    unused(status);
}

// 注册 SLE 连接管理回调
static void sle_conn_register_cbks(void)
{
    sle_connection_callbacks_t conn_cbks = {0};
    conn_cbks.connect_state_changed_cb = sle_connect_state_changed_cbk;
    conn_cbks.pair_complete_cb = sle_pair_complete_cbk;
    sle_connection_register_callbacks(&conn_cbks);
}

// ==================== 广播相关 ====================

// 广播使能回调
static void sle_announce_enable_cbk(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
}

// 广播禁用回调
static void sle_announce_disable_cbk(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
}

// SLE 使能完成回调
static void sle_enable_cbk(errcode_t status)
{
    unused(status);
}

// 注册 SLE 广播/发现相关回调
static void sle_announce_register_cbks(void)
{
    sle_announce_seek_callbacks_t seek_cbks = {0};
    seek_cbks.announce_enable_cb = sle_announce_enable_cbk;
    seek_cbks.announce_disable_cb = sle_announce_disable_cbk;
    seek_cbks.sle_enable_cb = sle_enable_cbk;
    sle_announce_seek_register_callbacks(&seek_cbks);
}

// 将设备名称填入广播数据
static uint16_t sle_set_adv_local_name(uint8_t *adv_data, uint16_t max_len)
{
    uint8_t index = 0;
    const char *local_name = SLE_DEVICE_NAME;
    uint8_t local_name_len = (uint8_t)strlen(local_name);

    adv_data[index++] = local_name_len + 1;
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&adv_data[index], max_len - index, local_name, local_name_len) != EOK) {
        return 0;
    }
    return (uint16_t)index + local_name_len;
}

// 组装 SLE 广播数据（发现等级）
static uint16_t sle_set_adv_data(uint8_t *adv_data)
{
    uint16_t idx = 0;

    // 发现等级
    struct sle_adv_common_value adv_disc_level = {
        .length = 2 - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    if (memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, sizeof(adv_disc_level)) != EOK) {
        return 0;
    }
    idx += sizeof(adv_disc_level);

    return idx;
}

// 组装 SLE 扫描响应数据（发射功率+设备名称）
static uint16_t sle_set_scan_response_data(uint8_t *scan_rsp_data)
{
    uint16_t idx = 0;

    // 发送功率
    struct sle_adv_common_value tx_power_level = {
        .length = 2 - 1,
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER,
    };
    if (memcpy_s(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &tx_power_level, sizeof(tx_power_level)) != EOK) {
        return 0;
    }
    idx += sizeof(tx_power_level);

    // 设备名称
    idx += sle_set_adv_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);

    return idx;
}

// 设置 SLE 广播参数和广播/扫描响应数据
static errcode_t sle_set_announce_param_and_data(void)
{
    uint8_t mac[SLE_ADDR_LEN] = CAR_LOCAL_MAC;

    // 设置广播参数
    sle_announce_param_t param = {0};
    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = SLE_ADV_HANDLE_DEFAULT;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_ADV_INTERVAL_MIN;
    param.announce_interval_max = SLE_ADV_INTERVAL_MAX;
    param.conn_interval_min = SLE_CONN_INTERVAL_MIN;
    param.conn_interval_max = SLE_CONN_INTERVAL_MAX;
    param.conn_max_latency = SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = SLE_CONN_TIMEOUT;
    param.announce_tx_power = SLE_ADV_TX_POWER;
    param.own_addr.type = 0;
    if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, mac, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    errcode_t ret = sle_set_announce_param(param.announce_handle, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 设置广播参数失败: %d\r\n", ret);
        return ret;
    }

    // 设置广播数据
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint16_t announce_data_len = sle_set_adv_data(announce_data);
    uint16_t seek_rsp_data_len = sle_set_scan_response_data(seek_rsp_data);

    sle_announce_data_t data = {0};
    data.announce_data = announce_data;
    data.announce_data_len = announce_data_len;
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = seek_rsp_data_len;

    ret = sle_set_announce_data(SLE_ADV_HANDLE_DEFAULT, &data);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 设置广播数据失败: %d\r\n", ret);
        return ret;
    }

    return ERRCODE_SLE_SUCCESS;
}

// ==================== 服务添加 ====================

// 添加 SLE GATT 服务
static errcode_t sle_add_service(void)
{
    errcode_t ret;
    sle_uuid_t service_uuid = {0};

    sle_uuid_set_u2(SLE_UUID_SERVICE, &service_uuid);
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 添加服务失败: %d\r\n", ret);
        return ERRCODE_SLE_FAIL;
    }

    printf("[SLE] 添加服务成功: handle=0x%x\r\n", g_service_handle);
    return ERRCODE_SLE_SUCCESS;
}

// 添加 SLE 数据特征（可读+可写+可通知）
static errcode_t sle_add_property(void)
{
    errcode_t ret;
    ssaps_property_info_t property = {0};

    // 初始化特征值
    (void)memset_s(g_property_value, sizeof(g_property_value), 0, sizeof(g_property_value));

    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    sle_uuid_set_u2(SLE_UUID_DATA, &property.uuid);
    property.value = g_property_value;
    property.value_len = sizeof(g_property_value);
    property.operate_indication =
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE | SSAP_OPERATE_INDICATION_BIT_NOTIFY;

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 添加特征失败: %d\r\n", ret);
        return ERRCODE_SLE_FAIL;
    }

    printf("[SLE] 添加特征成功: handle=0x%x\r\n", g_property_handle);
    return ERRCODE_SLE_SUCCESS;
}

// 注册 SSAP 服务端，添加服务和特征，设置 MTU 和连接参数
static errcode_t sle_add_service_and_property(void)
{
    errcode_t ret;
    sle_uuid_t app_uuid = {0};

    // 注册 SSAP 服务端
    app_uuid.len = 2;
    ssaps_register_server(&app_uuid, &g_server_id);

    // 添加服务
    if (sle_add_service() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }

    // 添加特征
    if (sle_add_property() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }

    // 启动服务
    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 启动服务失败: %d\r\n", ret);
        return ERRCODE_SLE_FAIL;
    }

    // 设置 MTU
    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 1;
    ssaps_set_info(g_server_id, &info);

    // 设置连接参数
    sle_default_connect_param_t conn_param = {0};
    conn_param.enable_filter_policy = 0;
    conn_param.gt_negotiate = 0;
    conn_param.initiate_phys = 1;
    conn_param.max_interval = SLE_CONN_INTERVAL_MAX;
    conn_param.min_interval = SLE_CONN_INTERVAL_MIN;
    conn_param.timeout = SLE_CONN_TIMEOUT;
    sle_default_connection_param_set(&conn_param);

    // 设置本地地址
    sle_addr_t addr = {0};
    uint8_t local_mac[SLE_ADDR_LEN] = CAR_LOCAL_MAC;
    addr.type = 0;
    if (memcpy_s(addr.addr, SLE_ADDR_LEN, local_mac, SLE_ADDR_LEN) == EOK) {
        sle_set_local_addr(&addr);
    }

    printf("[SLE] 服务端注册完成: server_id=%d\r\n", g_server_id);
    return ERRCODE_SLE_SUCCESS;
}

// ==================== 重启广播 Worker ====================
// 重启广播 worker：断连后延时再重启广播，避免阻塞协议栈回调
static int sle_adv_worker(void *arg)
{
    (void)arg;
    while (g_sle_worker_run) {
        if (osal_sem_down(&g_restart_sem) != OSAL_SUCCESS)
            continue;
        if (!g_sle_worker_run)
            break;
        osal_msleep(50);
        sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    }
    return 0;
}

// 启动重启广播 worker 任务（仅首次创建）
static void sle_adv_worker_start(void)
{
    if (!g_sle_lock_inited) {
        if (osal_mutex_init(&g_sle_lock) == OSAL_SUCCESS)
            g_sle_lock_inited = true;
    }
    if (!g_restart_sem_inited) {
        osal_sem_binary_sem_init(&g_restart_sem, 0);
        g_restart_sem_inited = true;
    }
    if (g_sle_worker != NULL)
        return;
    g_sle_worker_run = true;
    osal_kthread_lock();
    g_sle_worker = osal_kthread_create((osal_kthread_handler)sle_adv_worker, NULL, "sle_adv_w", 2048);
    if (g_sle_worker)
        osal_kthread_set_priority(g_sle_worker, 25);
    osal_kthread_unlock();
}

// ==================== 对外接口实现 ====================

// 初始化 SLE 设备：使能 SLE、注册回调、添加服务特征、启动广播
errcode_t sle_device_init(void)
{
    printf("[SLE] 初始化 SLE 设备...\r\n");

    // 启动重启广播 worker（必须在注册回调之前，断连可能立刻触发）
    sle_adv_worker_start();

    // 使能 SLE
    errcode_t ret = enable_sle();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 使能 SLE 失败: %d\r\n", ret);
        return ERRCODE_SLE_FAIL;
    }
    printf("[SLE] SLE 使能成功\r\n");

    // 注册回调
    sle_announce_register_cbks();
    sle_conn_register_cbks();
    sle_ssaps_register_cbks();

    // 添加服务和特征
    if (sle_add_service_and_property() != ERRCODE_SLE_SUCCESS) {
        return ERRCODE_SLE_FAIL;
    }

    // 设置广播参数和数据
    if (sle_set_announce_param_and_data() != ERRCODE_SLE_SUCCESS) {
        return ERRCODE_SLE_FAIL;
    }

    // 启动广播
    ret = sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 启动广播失败: %d\r\n", ret);
        return ERRCODE_SLE_FAIL;
    }

    printf("[SLE] SLE 设备初始化完成\r\n");
    return ERRCODE_SLE_SUCCESS;
}

// 注册 SLE 连接成功回调
void sle_device_register_connect_callback(sle_connect_cb_t cb)
{
    g_connect_cb = cb;
}

// 注册 SLE 断开连接回调
void sle_device_register_disconnect_callback(sle_disconnect_cb_t cb)
{
    g_disconnect_cb = cb;
}

// 注册 SLE 数据接收回调
void sle_device_register_data_callback(sle_data_recv_cb_t cb)
{
    g_data_recv_cb = cb;
}

// 通过 SLE 通知发送数据（锁内拿连接状态快照）
errcode_t sle_device_send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return ERRCODE_SLE_FAIL;
    }

    // 锁内拿连接状态快照，避免与断连回调竞争
    bool conn_snap;
    uint16_t conn_id_snap;
    if (g_sle_lock_inited)
        osal_mutex_lock(&g_sle_lock);
    conn_snap = g_connected;
    conn_id_snap = g_conn_id;
    if (g_sle_lock_inited)
        osal_mutex_unlock(&g_sle_lock);

    if (!conn_snap || conn_id_snap == 0xFFFF) {
        return ERRCODE_SLE_FAIL;
    }

    ssaps_ntf_ind_t param = {0};
    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = (uint8_t *)data;
    param.value_len = len;

    errcode_t ret = ssaps_notify_indicate(g_server_id, conn_id_snap, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE] 发送数据失败: %d\r\n", ret);
        return ERRCODE_SLE_FAIL;
    }

    return ERRCODE_SLE_SUCCESS;
}

// 查询 SLE 是否已连接
bool sle_device_is_connected(void)
{
    bool snap;
    if (g_sle_lock_inited)
        osal_mutex_lock(&g_sle_lock);
    snap = g_connected;
    if (g_sle_lock_inited)
        osal_mutex_unlock(&g_sle_lock);
    return snap;
}
