#include "ota_service.h"
#include "udp_net_common.h"
#include "upg.h"
#include "securec.h"
#include "soc_osal.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief OTA 升级状态枚举
 */
typedef enum {
    OTA_STATUS_IDLE = 0,      /* 空闲：未开始升级 */
    OTA_STATUS_PREPARED = 1,  /* 已准备：已通过 uapi_upg_prepare() 预留/初始化升级写入环境 */
    OTA_STATUS_RECEIVING = 2, /* 接收中：正在写入升级包数据 */
    OTA_STATUS_UPGRADING = 3, /* 升级中：数据写入完成，已触发设备侧升级 */
    OTA_STATUS_DONE = 4,      /* 完成：设备端升级流程结束（主要用于上报 UI 状态） */
    OTA_STATUS_ERROR = 5,     /* 错误：任意阶段出现错误（last_err 记录 uapi_upg_* 返回码） */
} ota_status_t;

/**
 * @brief OTA 升级上下文结构体
 */
typedef struct {
    ota_status_t status;   /* 当前升级状态 */
    uint32_t total_size;   /* 升级包总长度（由 0x10 Start 包携带） */
    uint32_t received_max; /* 当前已成功写入的最大偏移（用于计算进度/应答） */
    uint8_t percent;       /* 进度百分比 0~100，按 received_max/total_size 计算 */
    errcode_t last_err;    /* 最后一次操作的错误码 */
} ota_ctx_t;

static ota_ctx_t g_ota = {
    .status = OTA_STATUS_IDLE,
    .total_size = 0,
    .received_max = 0,
    .percent = 0,
    .last_err = ERRCODE_SUCC,
}; /* 全局 OTA 升级上下文 */

static errcode_t upg_backend_prepare(void *ctx, uint32_t total_size);
static errcode_t upg_backend_write(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len);
static errcode_t upg_backend_finish(void *ctx);
static errcode_t upg_backend_reset(void *ctx);

/* 默认 UPG 升级后端实现 */
static const ota_backend_t g_upg_ota_backend = {
    .prepare = upg_backend_prepare,
    .write = upg_backend_write,
    .finish = upg_backend_finish,
    .reset = upg_backend_reset,
};

static const ota_backend_t *g_ota_backend = &g_upg_ota_backend; /* 当前激活的 OTA 后端指针 */
static void *g_ota_backend_ctx = NULL;                          /* OTA 后端上下文指针 */

static osal_mutex g_ota_mutex;          /* 保护 OTA 状态的互斥锁 */
static bool g_ota_mutex_inited = false; /* 互斥锁是否已初始化 */

// 使用 robot_config.h 中的通用锁宏
#define OTA_LOCK()    MUTEX_LOCK(g_ota_mutex, g_ota_mutex_inited)
#define OTA_UNLOCK()  MUTEX_UNLOCK(g_ota_mutex, g_ota_mutex_inited)

/**
 * @brief OTA 升级内存分配适配函数
 * @param size 分配大小
 * @return 分配的内存指针
 * @note 将 UPG 库的内存分配接口适配到 OSAL
 */
static void *upg_malloc_port(uint32_t size)
{
    return osal_kmalloc(size, OSAL_GFP_ATOMIC);
}

/**
 * @brief OTA 升级内存释放适配函数
 * @param ptr 要释放的内存指针
 * @note 将 UPG 库的内存释放接口适配到 OSAL
 */
static void upg_free_port(void *ptr)
{
    osal_kfree(ptr);
}

/**
 * @brief OTA 升级串口输出适配函数
 * @param c 要输出的字符
 * @note 将 UPG 库的串口输出接口适配到 printf
 */
static void upg_putc_port(const char c)
{
    printf("%c", c);
}

/**
 * @brief 初始化 UPG 升级库（单次）
 * @note 将 OSAL 的内存分配和串口输出函数注册到 UPG 库
 *       如果已经初始化过则跳过，重复初始化返回 ERRCODE_UPG_ALREADY_INIT 视为成功
 */
static void ota_upg_init_once(void)
{
    // 将 UPG 库所需的内存与串口输出适配到 OSAL
    upg_func_t funcs = {
        .malloc = upg_malloc_port,
        .free = upg_free_port,
        .serial_putc = upg_putc_port,
    };

    errcode_t ret = uapi_upg_init(&funcs);
    if (ret != ERRCODE_SUCC && ret != ERRCODE_UPG_ALREADY_INIT) {
        g_ota.last_err = ret;
        g_ota.status = OTA_STATUS_ERROR;
        return;
    }
}

/**
 * @brief OTA 后端：准备升级环境
 * @param ctx 上下文（未使用）
 * @param total_size 升级包总大小
 * @return ERRCODE_SUCC 成功
 *         ERRCODE_INVALID_PARAM 参数无效
 *         其他 uapi_upg_prepare() 返回的错误码
 * @note 验证升级包大小，调用 uapi_upg_prepare() 预留升级存储空间
 */
static errcode_t upg_backend_prepare(void *ctx, uint32_t total_size)
{
    (void)ctx;

    if (g_ota.status == OTA_STATUS_ERROR) {
        // 如果上次是错误状态，但我们想重试，先强制重置为IDLE
        g_ota.status = OTA_STATUS_IDLE;
    }

    ota_upg_init_once();
    if (g_ota.status == OTA_STATUS_ERROR) {
        return g_ota.last_err;
    }

    if (total_size <= 4) {
        return ERRCODE_INVALID_PARAM;
    }

    uint32_t storage = uapi_upg_get_storage_size();
    uint32_t pkg_len = total_size - 4; // 跳过文件魔术字 0xDFADBEFF
    if (pkg_len == 0 || (storage != 0 && pkg_len > storage)) {
        return ERRCODE_INVALID_PARAM;
    }

    (void)uapi_upg_reset_upgrade_flag();
    upg_prepare_info_t info = {.package_len = pkg_len};
    return uapi_upg_prepare(&info);
}

/**
 * @brief OTA 后端：写入升级数据
 * @param ctx 上下文（未使用）
 * @param offset 数据偏移量
 * @param data 数据指针
 * @param len 数据长度
 * @return ERRCODE_SUCC 成功
 *         ERRCODE_INVALID_PARAM 参数无效
 *         其他 uapi_upg_write_package_sync() 返回的错误码
 * @note 支持带 0xDFADBEFF 魔术头的产线包格式和标准 UPG 包格式
 */
static errcode_t upg_backend_write(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (data == NULL || len == 0) {
        return ERRCODE_INVALID_PARAM;
    }
    uint32_t write_offset = offset;
    const uint8_t *write_data = data;
    uint16_t write_len = len;
    if (offset == 0) {
        if (len < 4) {
            return ERRCODE_INVALID_PARAM;
        }
        // 兼容带 0xDFADBEFF 头的产线包格式（需跳过4字节）和标准 UPG 包格式（不跳过）
        if (data[0] == 0xDF && data[1] == 0xAD && data[2] == 0xBE && data[3] == 0xEF) {
            write_offset = 0;
            write_data = data + 4;
            write_len = (uint16_t)(len - 4);
        } else {
            // 假设是标准 UPG 包，直接写入
            write_offset = 0;
            write_data = data;
            write_len = len;
        }
    } else {
        if (offset < 4) {
            return ERRCODE_INVALID_PARAM;
        }
        write_offset = offset - 4;
    }
    return uapi_upg_write_package_sync(write_offset, write_data, write_len);
}

/**
 * @brief OTA 后端：完成升级
 * @param ctx 上下文（未使用）
 * @return ERRCODE_SUCC 成功，已触发升级
 *         其他 uapi_upg_request_upgrade() 返回的错误码
 * @note 调用 uapi_upg_request_upgrade() 触发设备重启并执行升级
 */
static errcode_t upg_backend_finish(void *ctx)
{
    (void)ctx;
    return uapi_upg_request_upgrade(true);
}

/**
 * @brief OTA 后端：重置升级状态
 * @param ctx 上下文（未使用）
 * @return ERRCODE_SUCC 成功
 *         其他 uapi_upg_reset_upgrade_flag() 返回的错误码
 * @note 清除升级标志，允许重新开始升级流程
 */
static errcode_t upg_backend_reset(void *ctx)
{
    (void)ctx;
    return uapi_upg_reset_upgrade_flag();
}

/**
 * @brief 从大端序缓冲区读取 32 位整数
 * @param p 数据指针
 * @return 读取的 32 位整数
 */
static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * @brief 从大端序缓冲区读取 16 位整数
 * @param p 数据指针
 * @return 读取的 16 位整数
 */
static uint16_t read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief 将 32 位整数写入大端序缓冲区
 * @param p 数据指针
 * @param v 要写入的 32 位整数
 */
static void write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

/**
 * @brief 将 16 位整数写入大端序缓冲区
 * @param p 数据指针
 * @param v 要写入的 16 位整数
 */
static void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

/**
 * @brief 发送 OTA 应答包
 * @param to 目标地址
 * @param resp_code 应答码 (0=成功, 1=状态错误, 2=参数错误, 3=内部错误)
 * @param offset 当前接收到的最大偏移量
 * @return 0 成功，-1 失败
 * @note 应答包格式: [0x14][resp_code][offset_be][payload_len_be][status][percent][received_max][total_size][checksum]
 */
static int ota_send_response(const struct sockaddr_in *to, uint8_t resp_code, uint32_t offset)
{
    if (to == NULL)
        return -1;

    uint8_t buf[32]; // 足够小的栈缓冲区
    size_t payload_len = 10;
    size_t total_len = 9 + payload_len;
    if (total_len > sizeof(buf))
        return -1;

    buf[0] = 0x14;
    buf[1] = resp_code;
    write_u32_be(&buf[2], offset);
    write_u16_be(&buf[6], (uint16_t)payload_len);

    // payload: [status][percent][received_max][total_size]
    buf[8] = (uint8_t)g_ota.status;
    buf[9] = g_ota.percent;
    write_u32_be(&buf[10], g_ota.received_max);
    write_u32_be(&buf[14], g_ota.total_size);

    buf[total_len - 1] = udp_net_common_checksum8_add(buf, total_len - 1);
    return udp_net_common_send_to_addr(buf, total_len, to);
}

/**
 * @brief 解析 OTA 请求数据包
 * @param data 接收到的数据指针
 * @param len 数据长度
 * @param type 输出: 数据包类型 (0x10=Start, 0x11=Data, 0x12=End, 0x13=Ping)
 * @param cmd 输出: 命令字节 (保留)
 * @param offset 输出: 数据偏移量
 * @param payload 输出: 负载指针
 * @param payload_len 输出: 负载长度
 * @return true 解析成功，false 解析失败（校验错误或格式错误）
 * @note OTA 协议帧: [type][cmd][offset_be][payload_len_be][payload...][checksum]
 */
static bool ota_parse_request(const uint8_t *data,
                              size_t len,
                              uint8_t *type,
                              uint8_t *cmd,
                              uint32_t *offset,
                              const uint8_t **payload,
                              uint16_t *payload_len)
{
    if (data == NULL || len < 9 || type == NULL || cmd == NULL || offset == NULL || payload == NULL ||
        payload_len == NULL) {
        return false;
    }

    uint8_t chk = udp_net_common_checksum8_add(data, len - 1);
    if (chk != data[len - 1]) {
        return false;
    }

    // OTA 协议帧:
    // [0]=type(0x10/0x11/0x12/0x13), [1]=cmd(预留), [2..5]=offset, [6..7]=payload_len, [8..]=payload, [last]=checksum
    *type = data[0];
    *cmd = data[1];
    *offset = read_u32_be(&data[2]);
    uint16_t declared_len = read_u16_be(&data[6]);

    if (len != (size_t)(9 + declared_len)) {
        return false;
    }

    *payload_len = declared_len;
    *payload = &data[8];
    return true;
}

/**
 * @brief 初始化 OTA 服务
 * @note 初始化互斥锁和 UPG 库，如果已经初始化过则跳过
 */
void ota_service_init(void)
{
    if (g_ota_mutex_inited)
        return;

    if (osal_mutex_init(&g_ota_mutex) == OSAL_SUCCESS)
        g_ota_mutex_inited = true;
    else
        printf("ota_service: 互斥锁初始化失败\r\n");

    ota_upg_init_once();
}

/**
 * @brief 注册自定义 OTA 后端
 * @param backend 后端接口指针，NULL 表示使用默认 UPG 后端
 * @param ctx 上下文指针
 * @note 允许用户替换默认的 UPG 后端，实现自定义的升级存储方式
 */
void ota_service_register_backend(const ota_backend_t *backend, void *ctx)
{
    OTA_LOCK();
    if (backend == NULL) {
        g_ota_backend = &g_upg_ota_backend;
        g_ota_backend_ctx = NULL;
    } else {
        g_ota_backend = backend;
        g_ota_backend_ctx = ctx;
    }
    OTA_UNLOCK();
}

/**
 * @brief 处理接收到的 UDP 数据包，如果是 OTA 相关包则处理之
 * @param data 接收到的数据指针
 * @param len 数据长度
 * @param from_addr 发送方地址
 * @return true 如果是 OTA 数据包并已处理，false 如果不是 OTA 数据包
 * @note 处理 0x10(Start), 0x11(Data), 0x12(End), 0x13(Ping) 四种 OTA 包类型
 *       自动应答每个 OTA 请求，并管理升级状态机
 */
bool ota_service_process_packet(const uint8_t *data, size_t len, const struct sockaddr_in *from_addr)
{
    if (data == NULL || len == 0)
        return false;

    uint8_t type = data[0];
    // 检查是否为 OTA 协议类型 (0x10 ~ 0x13)
    if (type < 0x10 || type > 0x13) {
        return false;
    }

    uint8_t cmd = 0;
    uint32_t offset = 0;
    const uint8_t *payload = NULL;
    uint16_t payload_len = 0;

    if (!ota_parse_request(data, len, &type, &cmd, &offset, &payload, &payload_len)) {
        // 解析失败，但类型匹配，可能包损坏，视为已处理（防止被误判为其他包）或忽略
        return true;
    }

    OTA_LOCK(); // 加锁保护 OTA 状态

    if (type == 0x10) { // Start
        uint8_t resp = 0;
        if (payload_len != 4) {
            resp = 1;
        } else {
            uint32_t total_size = read_u32_be(payload);
            const ota_backend_t *backend = g_ota_backend;
            if (backend == NULL || backend->prepare == NULL) {
                resp = 3;
            } else {
                if (backend->reset != NULL) {
                    (void)backend->reset(g_ota_backend_ctx);
                }
                errcode_t ret = backend->prepare(g_ota_backend_ctx, total_size);
                if (ret != ERRCODE_SUCC) {
                    g_ota.status = OTA_STATUS_ERROR;
                    g_ota.last_err = ret;
                    resp = (ret == ERRCODE_INVALID_PARAM) ? 2 : 3;
                } else {
                    g_ota.status = OTA_STATUS_PREPARED;
                    g_ota.total_size = total_size;
                    g_ota.received_max = 0;
                    g_ota.percent = 0;
                    g_ota.last_err = ERRCODE_SUCC;
                    resp = 0;
                }
            }
        }
        (void)ota_send_response(from_addr, resp, 0);
    } else if (type == 0x11) { // Data
        if (offset != g_ota.received_max) {
            if (offset < g_ota.received_max) {
                // 重复包：发送成功ACK
                (void)ota_send_response(from_addr, 0, offset);
            }
            // 丢包：静默
            OTA_UNLOCK();
            return true;
        }

        uint8_t resp = 0;
        if (g_ota.status != OTA_STATUS_PREPARED && g_ota.status != OTA_STATUS_RECEIVING) {
            resp = 1;
        } else {
            const ota_backend_t *backend = g_ota_backend;
            if (backend == NULL || backend->write == NULL) {
                resp = 3;
            } else {
                errcode_t ret = backend->write(g_ota_backend_ctx, offset, payload, payload_len);
                if (ret != ERRCODE_SUCC) {
                    g_ota.status = OTA_STATUS_ERROR;
                    g_ota.last_err = ret;
                    resp = 3;
                } else {
                    g_ota.status = OTA_STATUS_RECEIVING;
                    g_ota.received_max += payload_len;
                    if (g_ota.total_size > 0) {
                        g_ota.percent = (uint8_t)((uint64_t)g_ota.received_max * 100 / g_ota.total_size);
                    }
                    resp = 0;
                }
            }
        }
        (void)ota_send_response(from_addr, resp, offset);
    } else if (type == 0x12) { // End
        uint8_t resp = 0;
        if (g_ota.status != OTA_STATUS_RECEIVING && g_ota.status != OTA_STATUS_PREPARED) {
            resp = 1;
        } else {
            const ota_backend_t *backend = g_ota_backend;
            if (backend == NULL || backend->finish == NULL) {
                resp = 3;
            } else {
                errcode_t ret = backend->finish(g_ota_backend_ctx);
                if (ret != ERRCODE_SUCC) {
                    g_ota.status = OTA_STATUS_ERROR;
                    g_ota.last_err = ret;
                    resp = 3;
                } else {
                    g_ota.status = OTA_STATUS_UPGRADING; // 升级中
                    resp = 0;
                }
            }
        }
        (void)ota_send_response(from_addr, resp, 0);
    } else if (type == 0x13) { // Ping
        (void)ota_send_response(from_addr, 0, g_ota.received_max);
    }

    OTA_UNLOCK();
    return true;
}
