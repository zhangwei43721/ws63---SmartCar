#ifndef CAR_COMMON_H
#define CAR_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "soc_osal.h"

#include "../../board_config.h"

// 通用互斥锁操作宏。调用前需确保 inited==true（init 失败应 panic）。
// 旧版在 inited==false 时"静默放行"让临界区裸跑；当前版打印一次 BUG 警告
// 便于定位漏 init 的 bug，但仍然不调用 osal_mutex_lock（传未初始化句柄可能 crash）。
#define MUTEX_LOCK(mutex, inited)                                                       \
    do {                                                                                \
        if ((inited)) {                                                                 \
            (void)osal_mutex_lock(&(mutex));                                            \
        } else {                                                                        \
            static int _mutex_warned_##__LINE__ = 0;                                    \
            if (!_mutex_warned_##__LINE__) {                                            \
                _mutex_warned_##__LINE__ = 1;                                           \
                printf("[BUG] mutex used before init @ %s:%d\r\n", __FILE__, __LINE__); \
            }                                                                           \
        }                                                                               \
    } while (0)

#define MUTEX_UNLOCK(mutex, inited)      \
    do {                                 \
        if (inited)                      \
            osal_mutex_unlock(&(mutex)); \
    } while (0)

// 封装 osal_kthread_lock/create/set_priority/unlock 4 步模板，
// 统一任务创建模式并保证 lock/unlock 配对。失败时打印 BUG 日志。
static inline osal_task *car_task_create_locked(const char *name,
                                                osal_kthread_handler entry,
                                                void *arg,
                                                unsigned int stack_size,
                                                unsigned int priority)
{
    osal_task *handle = NULL;
    osal_kthread_lock();
    handle = osal_kthread_create(entry, arg, name, stack_size);
    if (handle != NULL) {
        osal_kthread_set_priority(handle, priority);
    }
    osal_kthread_unlock();
    if (handle == NULL) {
        printf("[BUG] task create failed: %s\r\n", name ? name : "(null)");
    }
    return handle;
}

// 队满丢最旧→写入新消息。4 处复制模板的公共实现，避免全局暴露 queue handle。
static inline int osal_msgq_overwrite(unsigned long qid, unsigned int depth, const void *msg, unsigned int size)
{
    if (osal_msg_queue_get_msg_num(qid) >= depth) {
        unsigned char drop[64];
        unsigned int dsz = size;
        (void)osal_msg_queue_read_copy(qid, drop, &dsz, OSAL_MSGQ_NO_WAIT);
    }
    return osal_msg_queue_write_copy(qid, (void *)msg, size, OSAL_MSGQ_NO_WAIT);
}

/**
 * @brief 小车运行模式枚举
 */
typedef enum {
    CAR_STOP_STATUS = 0,           // 停止模式：小车停止运动
    CAR_TRACE_STATUS,              // 循迹模式：根据红外传感器进行黑线跟踪
    CAR_OBSTACLE_AVOIDANCE_STATUS, // 避障模式：根据超声波传感器自动避障
    CAR_WIFI_CONTROL_STATUS,       // 遥控模式：通过各种方式接收控制命令
} CarStatus;

// 模式切换命令（投递到 mode_q，由 car_main_task 消费）
typedef struct {
    CarStatus status;
    uint32_t source;
} ModeCmdMsg;

/**
 * @brief 机器人实时状态结构体
 * 用于向 Web 前端提供实时状态数据
 */
typedef struct {
    CarStatus mode;         // 当前模式 (0:Standby, 1:Trace, 2:Avoid, 3:Remote)
    float distance;         // 当前超声波距离 (cm)
    unsigned int ir_left;   // 左红外状态 (0:黑线, 1:白色)
    unsigned int ir_middle; // 中红外状态 (0:黑线, 1:白色)
    unsigned int ir_right;  // 右红外状态 (0:黑线, 1:白色)
} CarState;

// ---------- 模式命令来源 ----------
typedef enum {
    OTA_SUBCMD_START = 0x01,  // OTA命令
    MODE_SRC_BUTTON = 0x01,   // 按键 ISR
    MODE_SRC_UDP = 0x02,      // WiFi UDP 遥控
    MODE_SRC_HTTP = 0x03,     // 强制门户 HTTP
    MODE_SRC_SLE = 0x04,      // 星闪遥控
    MODE_SRC_VOICE = 0x05,    // UART/语音命令
    MODE_SRC_INTERNAL = 0x06, // 内部初始化等
} ModeCmdSource;

/**
 * @brief 协议包类型码（UDP / SLE / 强制门户共用 5 字节包格式）
 *        type 字段统一定义在此，禁止散落到各 .c 里魔数化。
 */
typedef enum {
    CAR_PKT_CONTROL = 0x01,       // 控制：[type, l_speed, r_speed, 0, 0]
    CAR_PKT_MODE = 0x03,          // 模式切换：[type, mode, 0, 0, 0]
    CAR_PKT_PID = 0x04,           // PID 设参：[type, k_type, val_hi, val_lo, save_flag]
    CAR_PKT_OTA = 0x05,           // OTA 触发：[type, sub_cmd, ...]
    CAR_PKT_WIFI_SET = 0xE0,      // WiFi 配置保存（不立即连接）
    CAR_PKT_WIFI_CONNECT = 0xE1,  // WiFi 配置并立即连接
    CAR_PKT_HEARTBEAT = 0xFE,     // 心跳
    CAR_PKT_DISCOVERY = 0xFF,     // 设备发现广播
} CarPktType;

/**
 * @brief PID 子参数索引（type=CAR_PKT_PID 时 cmd 字段）
 */
typedef enum {
    PID_PARAM_KP = 1,
    PID_PARAM_KI = 2,
    PID_PARAM_KD = 3,
    PID_PARAM_BASE_SPEED = 4,
} CarPidParam;

// ---------- 统一协议包体（UDP / SLE / 强制门户共用 5 字节格式）----------
#pragma pack(1)
typedef struct {
    uint8_t type;   // CarPktType 枚举值
    uint8_t cmd;    // 子命令 / 模式号 / PID 参数索引
    int8_t motor1;  // 左电机速度 (-100~100) / PID value_hi
    int8_t motor2;  // 右电机速度 (-100~100) / PID value_lo
    int8_t ir_data; // 红外数据 / 保留
} car_packet_t;
#pragma pack()

/**
 * @brief 统一协议处理入口，消费 CONTROL / MODE / HEARTBEAT
 * @return true  包已被消费
 * @return false 包未处理，调用方需自行处理（PID / OTA / WiFi 配置等）
 */
bool car_proto_handle_packet(const car_packet_t *pkt, uint32_t mode_source);

// ---------- 主状态机接口 ----------
bool car_mgr_post_mode(CarStatus status, uint32_t source); // 投递模式切换命令到状态机消息队列
void car_mgr_get_state_copy(CarState *out);                // 线程安全地获取 CarState 快照
void car_mgr_update_distance(float distance);              // 更新超声波距离值（避障模式写入）
void car_mgr_update_ir_status(unsigned int left, unsigned int middle,
                              unsigned int right); // 更新三路红外传感器状态

// ---------- 模式名字符串 ----------
const char *car_mode_name(CarStatus status); // 将模式枚举转为可读字符串

#endif
