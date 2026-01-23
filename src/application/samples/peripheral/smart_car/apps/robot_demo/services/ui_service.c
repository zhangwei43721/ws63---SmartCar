#include "ui_service.h"

static bool g_oled_ready = false; /* OLED 是否已初始化并可用 */

/**
 * @brief 模式显示信息结构体
 */
typedef struct {
    CarStatus status;  // 小车状态
    const char *line0; // 第 0 行显示文本（顶部）
    const char *line1; // 第 1 行显示文本（中部）
    const char *line2; // 第 2 行显示文本（底部）
} ModeDisplayInfo;

/*
 * 支持的字符:
 * 模, 式, 停, 止, 循, 迹, 避, 障, 遥, 控, 连, 接, 中, 成, 功, 失, 败, 等, 待, 热, 点, 配, 置
 */

/**
 * @brief 模式显示信息查找表
 */
static const ModeDisplayInfo g_mode_display[] = {
    {CAR_STOP_STATUS, "模式: 停止", "等待...", ""},
    {CAR_TRACE_STATUS, "模式: 循迹", "循迹中...", ""},
    {CAR_OBSTACLE_AVOIDANCE_STATUS, "模式: 避障", "避障中...", ""},
    {CAR_WIFI_CONTROL_STATUS, "模式: 遥控", "遥控中...", ""},
    {CAR_BT_CONTROL_STATUS, "模式: 蓝牙", "未启用", ""},
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
    if (!g_oled_ready) {
        return;
    }

    const ModeDisplayInfo *info = NULL;
    const char *line0 = "模式: 未知";
    const char *line1 = "重置中...";
    const char *line2 = "";

    // 查表法查找对应模式的显示信息
    for (size_t i = 0; i < sizeof(g_mode_display) / sizeof(g_mode_display[0]); i++) {
        if (g_mode_display[i].status == status) {
            info = &g_mode_display[i];
            break;
        }
    }

    if (info != NULL) {
        line0 = info->line0;
        line1 = info->line1;
        line2 = info->line2;
    }

    ssd1306_Fill(Black);
    ssd1306_DrawString16(0, 0, line0, White);
    ssd1306_DrawString16(0, 16, line1, White);
    ssd1306_DrawString16(0, 32, line2, White);
    ssd1306_UpdateScreen();
}

/**
 * @brief 在 OLED 上渲染待机页面
 * @param wifi_state WiFi 连接状态描述
 * @param ip_addr IP 地址字符串
 */
void ui_render_standby(const char *wifi_state, const char *ip_addr)
{
    ui_service_init();

    if (!g_oled_ready) {
        return;
    }

    // Convert English states to Chinese if possible
    const char *state_str = wifi_state;
    if (strstr(wifi_state, "Connecting")) {
        state_str = "WiFi: 连接中";
    } else if (strstr(wifi_state, "Connected")) {
        state_str = "WiFi: 连接成功";
    } else if (strstr(wifi_state, "Fail")) {
        state_str = "WiFi: 连接失败";
    } else if (strstr(wifi_state, "AP")) {
        state_str = "热点模式";
    }

    ssd1306_Fill(Black);
    ssd1306_DrawString16(0, 0, "模式: 停止", White);
    ssd1306_DrawString16(0, 16, state_str, White);

    // IP string is ASCII, but DrawString16 handles ASCII too
    ssd1306_DrawString16(0, 32, ip_addr, White);
    ssd1306_UpdateScreen();
}
