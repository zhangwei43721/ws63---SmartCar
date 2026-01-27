#include "ui_service.h"

static bool g_oled_ready = false; /* OLED 是否已初始化并可用 */

/**
 * @brief 模式显示信息结构体
 */
typedef struct {
    const char *line0; // 第 0 行显示文本（顶部）
    const char *line1; // 第 1 行显示文本（中部）
    const char *line2; // 第 2 行显示文本（底部）
} ModeDisplayInfo;

/*
 * 支持的字符:
 * 模, 式, 停, 止, 循, 迹, 避, 障, 遥, 控, 连, 接, 中, 成, 功, 失, 败, 等, 待, 热, 点, 配, 置
 */

/**
 * @brief 模式显示信息查找表（按 CarStatus 枚举值索引）
 */
static const ModeDisplayInfo g_mode_display[] = {
    // CAR_STOP_STATUS (0)
    {"模式: 停止", "等待...", ""},
    // CAR_TRACE_STATUS (1)
    {"模式: 循迹", "循迹中...", ""},
    // CAR_OBSTACLE_AVOIDANCE_STATUS (2)
    {"模式: 避障", "避障中...", ""},
    // CAR_WIFI_CONTROL_STATUS (3)
    {"模式: 遥控", "遥控中...", ""},
    // CAR_BT_CONTROL_STATUS (4)
    {"模式: 蓝牙", "未启用", ""},
};

/**
 * @brief 初始化 UI 服务（OLED 显示屏）
 * @note 初始化 I2C 总线和 SSD1306 显示屏
 */
void ui_service_init(void)
{
    if (g_oled_ready) {
        return;
    }

    uapi_pin_set_mode(ROBOT_I2C_SCL_PIN, ROBOT_I2C_PIN_MODE);
    uapi_pin_set_mode(ROBOT_I2C_SDA_PIN, ROBOT_I2C_PIN_MODE);

    errcode_t ret = uapi_i2c_master_init(ROBOT_I2C_BUS_ID, ROBOT_I2C_BAUDRATE, ROBOT_I2C_HS_CODE);
    if (ret != ERRCODE_SUCC) {
        printf("OLED: I2C 初始化失败，返回值=0x%x\r\n", ret);
        return;
    }

    ssd1306_Init();
    g_oled_ready = true;
}

/**
 * @brief 在 OLED 上显示当前模式页面
 * @param status 小车当前状态
 */
void ui_show_mode_page(CarStatus status)
{
    ui_service_init();
    if (!g_oled_ready)
        return;

    // 直接使用枚举值作为索引（更简单，不需要循环查找）
    int mode_count = (int)(sizeof(g_mode_display) / sizeof(g_mode_display[0]));
    if (status >= 0 && status < mode_count) {
        ssd1306_Fill(Black);
        ssd1306_DrawString16(0, 0, g_mode_display[status].line0, White);
        ssd1306_DrawString16(0, 16, g_mode_display[status].line1, White);
        ssd1306_DrawString16(0, 32, g_mode_display[status].line2, White);
        ssd1306_UpdateScreen();
    }
}

/**
 * @brief 在 OLED 上渲染待机页面
 * @param wifi_state WiFi 连接状态描述
 * @param ip_addr IP 地址字符串
 */
void ui_render_standby(WifiConnectStatus wifi_state, const char *ip_addr)
{
    ui_service_init();

    if (!g_oled_ready)
        return;

    const char *state_str = "WiFi: 未知状态";
    switch (wifi_state) {
        case WIFI_STATUS_DISCONNECTED:
            state_str = "WiFi: 未连接";
            break;
        case WIFI_STATUS_CONNECTING:
            state_str = "WiFi: 连接中";
            break;
        case WIFI_STATUS_CONNECTED:
            state_str = "WiFi: 连接成功";
            break;
        case WIFI_STATUS_AP_MODE:
            state_str = "热点模式";
            break;
        default:
            break;
    }

    ssd1306_Fill(Black);
    ssd1306_DrawString16(0, 0, "模式: 停止", White);
    ssd1306_DrawString16(0, 16, state_str, White);

    // IP string is ASCII, but DrawString16 handles ASCII too
    ssd1306_DrawString16(0, 32, ip_addr, White);
    ssd1306_UpdateScreen();
}
