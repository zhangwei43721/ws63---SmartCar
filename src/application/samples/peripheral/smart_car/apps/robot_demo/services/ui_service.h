#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/ssd1306/ssd1306.h"
#include "../robot_common.h"
#include "i2c.h"
#include "pinctrl.h"
#include "securec.h"

/* I2C 总线配置（用于 OLED 显示屏通信） */
#define ROBOT_I2C_BUS_ID 1         // I2C 总线编号
#define ROBOT_I2C_BAUDRATE 400000  // I2C 通信速率
#define ROBOT_I2C_HS_CODE 0x0      // I2C 高速模式(标志为0，即不使用)
#define ROBOT_I2C_SCL_PIN \
  15  // I2C 时钟线引脚（GPIO_15，连接 OLED 的 SCL 引脚）
#define ROBOT_I2C_SDA_PIN \
  16  // I2C 数据线引脚（GPIO_16，连接 OLED 的 SDA 引脚）
#define ROBOT_I2C_PIN_MODE 2  // GPIO 复用号

void ui_service_init(void);
void ui_show_mode_page(CarStatus status);
void ui_render_standby(WifiConnectStatus wifi_state, const char* ip_addr);
bool ui_service_is_ready(void);  // 查询 OLED 是否就绪

#endif
