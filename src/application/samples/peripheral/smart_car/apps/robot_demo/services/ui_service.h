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
#define ROBOT_I2C_BUS_ID 1        // I2C 总线编号
#define ROBOT_I2C_BAUDRATE 400000 // I2C 通信速率
#define ROBOT_I2C_HS_CODE 0x0     // I2C 高速模式(标志为0，即不使用)
#define ROBOT_I2C_SCL_PIN 15      // I2C 时钟线引脚（GPIO_15，连接 OLED 的 SCL 引脚）
#define ROBOT_I2C_SDA_PIN 16      // I2C 数据线引脚（GPIO_16，连接 OLED 的 SDA 引脚）
#define ROBOT_I2C_PIN_MODE 2      // GPIO 复用号

void ui_service_init(void);

/* 以下接口为非阻塞：仅向 UI 任务消息队列投递请求，由 UI 任务异步刷屏 */
void ui_show_mode_page(CarStatus status);
void ui_render_standby(WifiConnectStatus wifi_state, const char *ip_addr);
void ui_show_ota_progress(uint8_t percent, const char *status_line);

bool ui_service_is_ready(void); // 查询 OLED 是否就绪

/**
 * @brief 独占/释放 OLED（OTA 等场景使用）
 * @note 独占期间 ui_show_mode_page/ui_render_standby 会被忽略，
 *       避免与 OTA 进度页交替刷新导致屏幕闪烁
 */
void ui_service_acquire(void);
void ui_service_release(void);
bool ui_service_is_busy(void);

#endif
