#include "ui_service.h"

#include "../../../drivers/ssd1306/ssd1306.h"

#include "i2c.h"
#include "pinctrl.h"
#include "securec.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ROBOT_I2C_BUS_ID 1
#define ROBOT_I2C_BAUDRATE 400000
#define ROBOT_I2C_HS_CODE 0x0
#define ROBOT_I2C_SCL_PIN 15
#define ROBOT_I2C_SDA_PIN 16
#define ROBOT_I2C_PIN_MODE 2

static bool g_oled_ready = false;

void ui_service_init(void)
{
    if (g_oled_ready) {
        return;
    }

    uapi_pin_set_mode(ROBOT_I2C_SCL_PIN, ROBOT_I2C_PIN_MODE);
    uapi_pin_set_mode(ROBOT_I2C_SDA_PIN, ROBOT_I2C_PIN_MODE);

    errcode_t ret = uapi_i2c_master_init(ROBOT_I2C_BUS_ID, ROBOT_I2C_BAUDRATE, ROBOT_I2C_HS_CODE);
    if (ret != ERRCODE_SUCC) {
        printf("OLED: I2C init failed, ret=0x%x\r\n", ret);
        return;
    }

    ssd1306_Init();
    g_oled_ready = true;
}

void ui_show_mode_page(CarStatus status)
{
    ui_service_init();
    if (!g_oled_ready) {
        return;
    }

    char line0[32] = {0};
    char line1[32] = {0};
    char line2[32] = {0};

    switch (status) {
        case CAR_STOP_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Standby");
            snprintf(line1, sizeof(line1), "WiFi: Check...");
            snprintf(line2, sizeof(line2), "Press KEY1");
            break;
        case CAR_TRACE_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Trace");
            snprintf(line1, sizeof(line1), "Infrared ON");
            snprintf(line2, sizeof(line2), "KEY1 -> Next");
            break;
        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Obstacle");
            snprintf(line1, sizeof(line1), "Ultrasonic ON");
            snprintf(line2, sizeof(line2), "KEY1 -> Next");
            break;
        case CAR_WIFI_CONTROL_STATUS:
            snprintf(line0, sizeof(line0), "Mode: WiFi Ctrl");
            snprintf(line1, sizeof(line1), "Waiting cmd...");
            snprintf(line2, sizeof(line2), "KEY1 -> Stop");
            break;
        case CAR_BT_CONTROL_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Bluetooth");
            snprintf(line1, sizeof(line1), "Current: Disabled");
            snprintf(line2, sizeof(line2), "KEY1 -> Stop");
            break;
        default:
            snprintf(line0, sizeof(line0), "Mode: Unknown");
            snprintf(line1, sizeof(line1), "Resetting...");
            (void)memset_s(line2, sizeof(line2), 0, sizeof(line2));
            break;
    }

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString(line0, Font_7x10, White);
    ssd1306_SetCursor(0, 16);
    ssd1306_DrawString(line1, Font_7x10, White);
    ssd1306_SetCursor(0, 32);
    ssd1306_DrawString(line2, Font_7x10, White);
    ssd1306_UpdateScreen();
}

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
