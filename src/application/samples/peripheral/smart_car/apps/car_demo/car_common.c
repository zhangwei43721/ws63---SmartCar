/**
 * @file car_common.c
 * @brief 模式名 / WiFi 状态文本集中查表，供 UI / 主任务 / Web 复用，以及统一的协议包分发与控制网关
 */

#include "car_common.h"
#include <stdio.h>
#include <string.h>

#include "../../drivers/motor_control/bsp_motor.h"
#include "core/mode_trace.h"
#include "services/ota_service.h"
#include "services/udp_service.h"

// 安全网关决策逻辑。在此处集中管理手动驾驶的许可条件，避免指令在自动与遥控模式之间打架冲突。
// 仅在手动遥控模式（CAR_WIFI_CONTROL_STATUS），或者循迹模式下的传感器校准状态下允许手动操作。
bool car_mgr_is_manual_allowed(void)
{
    CarState st;
    car_mgr_get_state_copy(&st);
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
void car_mgr_manual_drive(int8_t left, int8_t right, uint32_t source)
{
    if (car_mgr_is_manual_allowed()) {
        bsp_motor_push_cmd(left, right);
    } else {
        printf("[Safety] Intercepted manual drive command from source %u (%d, %d)\r\n", source, left, right);
    }
}

// 统一协议包分发中心。将原本散落在各个通信协议层（如
// UDP、星闪）中的报文解析与分发逻辑集中到此处处理，实现不同通信媒介的协议解析复用。
bool car_proto_handle_packet(const uint8_t *data, uint16_t len, uint32_t mode_source)
{
    if (!data || len < 1)
        return false;

    switch (data[0]) {
        case CAR_PKT_CONTROL: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                car_mgr_manual_drive(pkt->motor1, pkt->motor2, mode_source);
            }
            return true;
        }

        case CAR_PKT_MODE: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                if (pkt->cmd <= CAR_WIFI_CONTROL_STATUS) {
                    car_mgr_post_mode((CarStatus)pkt->cmd, mode_source);
                }
            }
            return true;
        }

        case CAR_PKT_PID: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                mode_trace_set_pid(pkt->cmd, (int16_t)((pkt->motor1 << 8) | (uint8_t)pkt->motor2));
            }
            return true;
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
                udp_service_send_data((const uint8_t *)&ack, sizeof(ack));
            }
            return true;
        }

        case CAR_PKT_TRACE_CALIB: {
            if (len == 7) {
                uint16_t l = (uint16_t)((data[1] << 8) | data[2]);
                uint16_t m = (uint16_t)((data[3] << 8) | data[4]);
                uint16_t r = (uint16_t)((data[5] << 8) | data[6]);
                mode_trace_update_thresholds(l, m, r);
            }
            return true;
        }

        case CAR_PKT_TRACE_SUBMODE: {
            if (len == sizeof(car_packet_t)) {
                const car_packet_t *pkt = (const car_packet_t *)data;
                mode_trace_set_submode(pkt->cmd);
            }
            return true;
        }

        case CAR_PKT_HEARTBEAT:
            return true;

        default:
            return false;
    }
}

static const char *const k_mode_names[] = {
    "停止",
    "循迹",
    "避障",
    "遥控",
};

// 将CarStatus枚举值转换为中文模式名称字符串
const char *car_mode_name(CarStatus status)
{
    unsigned idx = (unsigned)status;
    if (idx < sizeof(k_mode_names) / sizeof(k_mode_names[0]))
        return k_mode_names[idx];
    return "?";
}
