/**
 * @file car_ctrl.c
 * @brief 控制中枢：安全驾驶网关 + 统一命令总线（命令队列 + 串行消费任务）
 * @details 原本散落在 car_common.c 与各通信协议层（UDP、星闪、HTTP、语音）中的
 *          控制审核与报文分发逻辑集中于此，实现"通道只翻译，中枢做决策"。
 *          二进制协议包经 car_ctrl_post_cmd 入队，由本文件的专用任务串行处理，
 *          应答通过通道注入的 reply 回调原路返回，core 不反向依赖任何 channels。
 */

#include "car_ctrl.h"

#include <stdio.h>
#include <string.h>

#include "../car_common.h"
#include "car_state.h"
#include "mode_mgr.h"
#include "mode_trace.h"
#include "../../../drivers/motor_control/bsp_motor.h"
#include "../services/ota_service.h"
#include "securec.h"
#include "soc_osal.h"

// ---------- 命令总线内部状态 ----------
#define CAR_CTRL_QUEUE_DEPTH 8 // 命令总线队列深度

static unsigned long g_ctrl_queue = 0;   // 命令总线队列 ID
static bool g_ctrl_queue_inited = false; // 命令队列是否已初始化

/* ============================================================
 * 安全驾驶网关
 * ============================================================ */

// 安全网关决策逻辑。在此处集中管理手动驾驶的许可条件，避免指令在自动与遥控模式之间打架冲突。
// 仅在手动遥控模式（CAR_WIFI_CONTROL_STATUS），或者循迹模式下的传感器校准状态下允许手动操作。
bool car_ctrl_is_manual_allowed(void)
{
    CarState st;
    car_state_get_copy(&st);
    if (st.mode == CAR_WIFI_CONTROL_STATUS) {
        return true;
    }
    if (st.mode == CAR_TRACE_STATUS && mode_trace_is_calibrating()) {
        return true;
    }
    return false;
}

// 安全驾驶网关入口。所有外部控制命令（如 HTTP、语音串口、WiFi
// UDP、星闪）必须经此入口审核放行后，方可输出给底层的电机驱动。
// 注意：停车 (0,0) 同样遵守该规则——自动模式拥有自己的电机，外部通道无权干预；
// 遥控模式下链路中断的自动停车由 bsp_motor 的 400ms 看门狗兜底。
void car_ctrl_manual_drive(int8_t left, int8_t right, uint32_t source)
{
    if (car_ctrl_is_manual_allowed()) {
        bsp_motor_push_cmd(left, right);
    } else {
        printf("[Safety] Intercepted manual drive command from source %u (%d, %d)\r\n", source, left, right);
    }
}

/* ============================================================
 * 统一协议包处理（仅由总线消费任务调用）
 * ============================================================ */

// 统一协议包分发。应答包经 reply 回调交由来源通道发出，实现"从哪个通道来，ACK 回哪个通道去"。
static void car_ctrl_handle_packet(const uint8_t *data, uint16_t len, uint32_t source, car_reply_fn reply,
                                   void *reply_ctx)
{
    if (!data || len < 1)
        return;

    switch (data[0]) {
        case CAR_PKT_CONTROL: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                car_ctrl_manual_drive(pkt->motor1, pkt->motor2, source);
            }
            break;
        }

        case CAR_PKT_MODE: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                if (pkt->cmd <= CAR_WIFI_CONTROL_STATUS) {
                    mode_mgr_post((CarStatus)pkt->cmd, source);
                }
            }
            break;
        }

        case CAR_PKT_PID: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                mode_trace_set_pid(pkt->cmd, (int16_t)((pkt->motor1 << 8) | (uint8_t)pkt->motor2));
            }
            break;
        }

        case CAR_PKT_OTA: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                car_packet_t ack = {0};
                ack.type = CAR_PKT_OTA;
                if (pkt->cmd == OTA_SUBCMD_START) {
                    ack.cmd = ota_service_start(0) ? 0x00 : 0x01;
                } else if (pkt->cmd == 0x02) {
                    ota_service_cancel();
                    ack.cmd = 0x00;
                } else {
                    ack.cmd = 0x02;
                }
                if (reply) {
                    reply(reply_ctx, (const uint8_t *)&ack, sizeof(ack));
                }
            }
            break;
        }

        case CAR_PKT_TRACE_CALIB: {
            if (len == 7) {
                uint16_t l = (uint16_t)((data[1] << 8) | data[2]);
                uint16_t m = (uint16_t)((data[3] << 8) | data[4]);
                uint16_t r = (uint16_t)((data[5] << 8) | data[6]);
                mode_trace_update_thresholds(l, m, r);
            }
            break;
        }

        case CAR_PKT_TRACE_SUBMODE: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                mode_trace_set_submode(pkt->cmd);
            }
            break;
        }

        case CAR_PKT_HEARTBEAT:
            break;

        default:
            printf("[CarCtrl] 未知包类型: 0x%02X (来源 %u, 长度 %d)\r\n", data[0], source, len);
            break;
    }
}

/* ============================================================
 * 命令总线：队列 + 消费任务
 * ============================================================ */

// 总线消费任务：串行处理各通道投递的协议包，无消息时阻塞休眠
static int car_ctrl_task(void *arg)
{
    (void)arg;

    while (1) {
        car_cmd_t cmd;
        unsigned int sz = sizeof(cmd);
        int ret = osal_msg_queue_read_copy(g_ctrl_queue, &cmd, &sz, OSAL_WAIT_FOREVER);
        if (ret != OSAL_SUCCESS || sz != sizeof(cmd))
            continue;

        car_ctrl_handle_packet(cmd.data, cmd.len, cmd.source, cmd.reply, cmd.reply_ctx);
    }
    return 0;
}

// 创建命令总线队列与消费任务（仅执行一次）
void car_ctrl_init(void)
{
    if (g_ctrl_queue_inited)
        return;
    if (osal_msg_queue_create("car_ctrl", CAR_CTRL_QUEUE_DEPTH, &g_ctrl_queue, 0, sizeof(car_cmd_t)) !=
        OSAL_SUCCESS) {
        printf("[CarCtrl] 命令队列创建失败\r\n");
        return;
    }
    g_ctrl_queue_inited = true;

    (void)car_task_create_locked("car_ctrl", (osal_kthread_handler)car_ctrl_task, NULL, 3072, 24);
}

// 投递命令到总线（数据整体拷贝，调用方的栈缓冲可立即复用）
bool car_ctrl_post_cmd(const car_cmd_t *cmd)
{
    if (!g_ctrl_queue_inited || cmd == NULL || cmd->len == 0 || cmd->len > CAR_CMD_MAX_PAYLOAD)
        return false;

    return (osal_msgq_overwrite(g_ctrl_queue, CAR_CTRL_QUEUE_DEPTH, cmd, sizeof(*cmd)) == OSAL_SUCCESS);
}
