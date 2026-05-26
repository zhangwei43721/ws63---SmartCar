/**
 * @file robot_common.c
 * @brief 模式名 / WiFi 状态文本集中查表，供 UI / 主任务 / Web 复用
 */

#include "robot_common.h"

#include "../../drivers/motor_control/bsp_motor.h"

bool robot_proto_handle_packet(const robot_packet_t *pkt, uint32_t mode_source)
{
    if (!pkt)
        return false;

    switch (pkt->type) {
        case ROBOT_PKT_CONTROL:
            bsp_motor_push_cmd(pkt->motor1, pkt->motor2);
            return true;

        case ROBOT_PKT_MODE:
            if (pkt->cmd <= CAR_WIFI_CONTROL_STATUS) {
                robot_mgr_post_mode((CarStatus)pkt->cmd, mode_source);
            }
            return true;

        case ROBOT_PKT_HEARTBEAT:
            return true;

        default:
            return false;
    }
}

static const char *const k_mode_names[] = {
    "停止", "循迹", "避障", "遥控",
};

static const char *const k_wifi_status[] = {
    "未连接", "连接中", "已连接", "热点模式",
};

const char *robot_mode_name(CarStatus status)
{
    unsigned idx = (unsigned)status;
    if (idx < sizeof(k_mode_names) / sizeof(k_mode_names[0]))
        return k_mode_names[idx];
    return "?";
}

const char *robot_wifi_status_text(WifiConnectStatus s)
{
    unsigned idx = (unsigned)s;
    if (idx < sizeof(k_wifi_status) / sizeof(k_wifi_status[0]))
        return k_wifi_status[idx];
    return "?";
}
