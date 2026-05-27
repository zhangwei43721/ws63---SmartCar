#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../robot_common.h"

void ui_service_init(void);

// 以下接口为非阻塞：仅向 UI 任务消息队列投递请求，由 UI 任务异步刷屏
void ui_show_mode_page(CarStatus status);
void ui_show_ota_progress(uint8_t percent, const char *status_line);

bool ui_service_is_ready(void); // 查询 OLED 是否就绪

/**
 * @brief 独占/释放 OLED（OTA 等场景使用）
 * @note 独占期间 ui_show_mode_page 会被忽略，
 *       避免与 OTA 进度页交替刷新导致屏幕闪烁
 */
void ui_service_acquire(void);
void ui_service_release(void);

#endif
