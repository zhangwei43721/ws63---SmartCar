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
            printf("[BUG] mutex used before init @ %s:%d\r\n", __FILE__, __LINE__);     \
        }                                                                               \
    } while (0)

#define MUTEX_UNLOCK(mutex, inited)      \
    do {                                 \
        if (inited)                      \
            osal_mutex_unlock(&(mutex)); \
    } while (0)

// 任务与队列公共辅助函数
osal_task *car_task_create_locked(const char *name,
                                  osal_kthread_handler entry,
                                  void *arg,
                                  unsigned int stack_size,
                                  unsigned int priority);

int osal_msgq_overwrite(unsigned long qid, unsigned int depth, const void *msg, unsigned int size);

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
    
    // 循迹探头原始电压与阈值（用于上位机网页校准）
    uint32_t adc_left;
    uint32_t adc_middle;
    uint32_t adc_right;
    uint16_t th_left;
    uint16_t th_middle;
    uint16_t th_right;
} CarState;

// ---------- 模式命令来源 ----------
typedef enum {
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
    CAR_PKT_TRACE_INFO = 0x0A,    // 循迹 Raw/Th 遥测：[type, rawL_hi, rawL_lo, rawM_hi, rawM_lo, rawR_hi, rawR_lo, thL_hi, thL_lo, thM_hi, thM_lo, thR_hi, thR_lo]
    CAR_PKT_LOG_DATA = 0x0B,      // 虚拟串口日志包：[type, text...]
    CAR_PKT_TRACE_CALIB = 0x0C,   // 循迹探头校准包：[type, thL_hi, thL_lo, thM_hi, thM_lo, thR_hi, thR_lo]
    CAR_PKT_TRACE_SUBMODE = 0x0D, // 循迹子模式：[type, submode, 0, 0, 0]
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
 * @brief 统一协议处理入口，消费 CONTROL / MODE / HEARTBEAT / PID / OTA / TRACE_CALIB / TRACE_SUBMODE 等
 * @return true  包已被消费
 * @return false 包未处理，调用方需自行处理
 */
bool car_proto_handle_packet(const uint8_t *data, uint16_t len, uint32_t mode_source);

// 手动驾驶安全网关接口
void car_mgr_manual_drive(int8_t left, int8_t right, uint32_t source);
bool car_mgr_is_manual_allowed(void);

// ---------- 主状态机接口 ----------
bool car_mgr_post_mode(CarStatus status, uint32_t source); // 投递模式切换命令到状态机消息队列
void car_mgr_get_state_copy(CarState *out);                // 线程安全地获取 CarState 快照
void car_mgr_update_distance(float distance);              // 更新超声波距离值（避障模式写入）
void car_mgr_update_ir_status(unsigned int left, unsigned int middle,
                              unsigned int right); // 更新三路红外传感器状态
void car_mgr_update_adc_values(uint32_t left, uint32_t middle, uint32_t right); // 更新原始采样 ADC
void car_mgr_update_thresholds(uint16_t left, uint16_t middle, uint16_t right); // 更新当前活跃阈值

// ---------- 模式名字符串 ----------
const char *car_mode_name(CarStatus status); // 将模式枚举转为可读字符串

#endif
