/**
 * @file car_common.c
 * @brief 模式名 / WiFi 状态文本集中查表，供 UI / 主任务 / Web 复用
 */

#include "car_common.h"

#include "../../drivers/motor_control/bsp_motor.h"

/* 解析并分发机器人协议数据包（控制/模式切换/心跳） */
bool car_proto_handle_packet(const car_packet_t *pkt, uint32_t mode_source)
{
    if (!pkt)
        return false;

    switch (pkt->type) {
        case CAR_PKT_CONTROL:
            bsp_motor_push_cmd(pkt->motor1, pkt->motor2);
            return true;

        case CAR_PKT_MODE:
            if (pkt->cmd <= CAR_WIFI_CONTROL_STATUS)
                car_mgr_post_mode((CarStatus)pkt->cmd, mode_source);

            return true;

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

/* 将CarStatus枚举值转换为中文模式名称字符串 */
const char *car_mode_name(CarStatus status)
{
    unsigned idx = (unsigned)status;
    if (idx < sizeof(k_mode_names) / sizeof(k_mode_names[0]))
        return k_mode_names[idx];
    return "?";
}
