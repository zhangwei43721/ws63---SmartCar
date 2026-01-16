#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include "../robot_common.h"
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

void ui_service_init(void);
void ui_show_mode_page(CarStatus status);
void ui_render_standby(const char *wifi_state, const char *ip_addr);

#endif
