#include "ui_service.h"

static bool g_oled_ready = false;

/**
 * @brief 模式显示信息结构体
 */
typedef struct {
    CarStatus status;
    const char *line0;
    const char *line1;
    const char *line2;
} ModeDisplayInfo;

/**
 * @brief 模式显示信息查找表
 */
static const ModeDisplayInfo g_mode_display[] = {
    {CAR_STOP_STATUS, "Mode: Standby", "WiFi: Check...", "Press KEY1"},
    {CAR_TRACE_STATUS, "Mode: Trace", "Infrared ON", "KEY1 -> Next"},
    {CAR_OBSTACLE_AVOIDANCE_STATUS, "Mode: Obstacle", "Ultrasonic ON", "KEY1 -> Next"},
    {CAR_WIFI_CONTROL_STATUS, "Mode: WiFi Ctrl", "Waiting cmd...", "KEY1 -> Stop"},
    {CAR_BT_CONTROL_STATUS, "Mode: Bluetooth", "Current: Disabled", "KEY1 -> Stop"},
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
    const char *line0 = "Mode: Unknown";
    const char *line1 = "Resetting...";
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
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString((char *)line0, Font_7x10, White);
    ssd1306_SetCursor(0, 16);
    ssd1306_DrawString((char *)line1, Font_7x10, White);
    ssd1306_SetCursor(0, 32);
    ssd1306_DrawString((char *)line2, Font_7x10, White);
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

    if (wifi_state == NULL || ip_addr == NULL) {
        return;
    }

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString("Mode: Standby", Font_7x10, White);
    ssd1306_SetCursor(0, 16);
    ssd1306_DrawString((char *)wifi_state, Font_7x10, White);
    ssd1306_SetCursor(0, 32);
    ssd1306_DrawString((char *)ip_addr, Font_7x10, White);
    ssd1306_UpdateScreen();
}
