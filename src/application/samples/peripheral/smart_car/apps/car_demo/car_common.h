#ifndef CAR_COMMON_H
#define CAR_COMMON_H

// 本文件只存放跨 core / channels / services 共享的【类型与协议定义】。
// 控制逻辑见 core/car_ctrl.h，状态仓库见 core/car_state.h，模式状态机见 core/mode_mgr.h。

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

// 模式切换命令（投递到 mode_q，由 mode_mgr 状态机任务消费）
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
    CAR_PKT_CONTROL = 0x01,       // 控制：[type, dir, speed, 0, 0]（dir=CarDriveCmd 方向，speed=速度幅值）
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

/**
 * @brief 遥控驾驶方向命令（手动驾驶的语义层）
 *        前端/语音只上报"按下哪个按钮"，具体转向动作（差速 or 舵机）
 *        由嵌入式按底盘类型翻译，速度幅值也由嵌入式固定。
 * @note  枚举值刻意与 voice_channel.h 的 VoiceCommand 对齐，便于直通翻译。
 */
typedef enum {
    CAR_DRIVE_STOP = 0,     // 停车
    CAR_DRIVE_FORWARD = 1,  // 前进
    CAR_DRIVE_BACKWARD = 2, // 后退
    CAR_DRIVE_LEFT = 3,     // 左转（舵机底盘=前进+左满舵；差速底盘=原地左转）
    CAR_DRIVE_RIGHT = 4,    // 右转（舵机底盘=前进+右满舵；差速底盘=原地右转）
} CarDriveCmd;

// ---------- 统一协议包体（UDP / SLE / 强制门户共用 5 字节格式）----------
#pragma pack(1)
typedef struct {
    uint8_t type;   // CarPktType 枚举值
    uint8_t cmd;    // 子命令 / 模式号 / PID 参数索引
    int8_t motor1;  // 方向命令时为 speed / PID value_hi / 心跳 distance
    int8_t motor2;  // PID value_lo（方向命令时保留为 0）
    int8_t ir_data; // 红外数据 / 保留
} car_packet_t;
#pragma pack()

#endif
