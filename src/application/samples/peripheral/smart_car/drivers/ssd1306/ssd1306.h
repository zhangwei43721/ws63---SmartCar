/*
 * Copyright (c) 2024 HiSilicon Technologies CO., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BSP_SSD1306_H
#define BSP_SSD1306_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ssd1306_fonts.h"

// --- 显示尺寸 ---
#ifndef SSD1306_HEIGHT
#define SSD1306_HEIGHT 64
#endif

#ifndef SSD1306_WIDTH
#define SSD1306_WIDTH 128
#endif

#ifndef SSD1306_BUFFER_SIZE
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)
#endif

// ================= SSD1306 命令字 =================
// --- 基本命令 ---
#define SSD1306_CMD_SET_CONTRAST          0x81
#define SSD1306_CMD_ENTIRE_ON_RESUME      0xA4
#define SSD1306_CMD_ENTIRE_ON_FORCE       0xA5
#define SSD1306_CMD_NORMAL_DISPLAY        0xA6
#define SSD1306_CMD_INVERT_DISPLAY        0xA7
#define SSD1306_CMD_DISPLAY_OFF           0xAE
#define SSD1306_CMD_DISPLAY_ON            0xAF

// --- 寻址 ---
#define SSD1306_CMD_SET_MEM_ADDR_MODE     0x20
#define SSD1306_CMD_SET_COL_ADDR          0x21
#define SSD1306_CMD_SET_PAGE_ADDR         0x22
#define SSD1306_CMD_SET_PAGE_START        0xB0

// --- 硬件配置 ---
#define SSD1306_CMD_SET_START_LINE        0x40
#define SSD1306_CMD_SET_SEG_REMAP_0       0xA0
#define SSD1306_CMD_SET_SEG_REMAP_127     0xA1
#define SSD1306_CMD_SET_MULTIPLEX         0xA8
#define SSD1306_CMD_SET_COM_SCAN_NORMAL   0xC0
#define SSD1306_CMD_SET_COM_SCAN_REMAP    0xC8
#define SSD1306_CMD_SET_DISPLAY_OFFSET    0xD3
#define SSD1306_CMD_SET_COM_PINS          0xDA

// --- 时序 ---
#define SSD1306_CMD_SET_CLOCK_DIV         0xD5
#define SSD1306_CMD_SET_PRECHARGE         0xD9
#define SSD1306_CMD_SET_VCOM_DESELECT     0xDB
#define SSD1306_CMD_CHARGE_PUMP           0x8D

// --- 参数值 ---
#define SSD1306_ADDR_MODE_HORIZONTAL      0x00
#define SSD1306_ADDR_MODE_VERTICAL        0x01
#define SSD1306_ADDR_MODE_PAGE            0x02
#define SSD1306_CHARGE_PUMP_ENABLE        0x14
#define SSD1306_COM_PINS_ALT_NOCFG        0x02
#define SSD1306_COM_PINS_ALT_CFG          0x12
#define SSD1306_CLOCK_DIV_F0              0xF0
#define SSD1306_PRECHARGE_1DCLK           0x11
#define SSD1306_VCOM_83X_VCC              0x30
#define SSD1306_MUX_32                    0x1F
#define SSD1306_MUX_64                    0x3F

// --- 显示模式 ---
#define SSD1306_COL_START                 0x00
#define SSD1306_COL_END                   0x7F
#define SSD1306_PAGE_START                0x00
#define SSD1306_PAGE_END                  0x07

// ================= 类型定义 =================

typedef enum {
    BSP_SSD1306_COLOR_BLACK = 0x00,
    BSP_SSD1306_COLOR_WHITE = 0x01
} BspSsd1306Color;

typedef enum {
    BSP_SSD1306_OK = 0x00,
    BSP_SSD1306_ERR = 0x01
} BspSsd1306Error;

typedef struct {
    uint16_t CurrentX;
    uint16_t CurrentY;
    uint8_t Inverted;
    uint8_t Initialized;
    uint8_t DisplayOn;
} BspSsd1306Ctx;

typedef struct {
    uint8_t x;
    uint8_t y;
} BspSsd1306Vertex;

// ================= API =================
bool bsp_ssd1306_init(void);
void bsp_ssd1306_fill(BspSsd1306Color color);
void bsp_ssd1306_set_cursor(uint8_t x, uint8_t y);
void bsp_ssd1306_update_screen(void);

char bsp_ssd1306_draw_char(char ch, FontDef Font, BspSsd1306Color color);
char bsp_ssd1306_draw_string(char *str, FontDef Font, BspSsd1306Color color);
void bsp_ssd1306_draw_string16(uint8_t x, uint8_t y, const char *str, BspSsd1306Color color);

void bsp_ssd1306_draw_pixel(uint8_t x, uint8_t y, BspSsd1306Color color);
void bsp_ssd1306_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, BspSsd1306Color color);
void bsp_ssd1306_draw_polyline(const BspSsd1306Vertex *par_vertex, uint16_t par_size, BspSsd1306Color color);
void bsp_ssd1306_draw_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, BspSsd1306Color color);
void bsp_ssd1306_draw_circle(uint8_t par_x, uint8_t par_y, uint8_t par_r, BspSsd1306Color par_color);
void bsp_ssd1306_draw_bitmap(const uint8_t *bitmap, uint32_t size);
void bsp_ssd1306_draw_region(uint8_t x, uint8_t y, uint8_t w, const uint8_t *data, uint32_t size);

void bsp_ssd1306_set_contrast(uint8_t value);
void bsp_ssd1306_set_display_on(uint8_t on);
uint8_t bsp_ssd1306_get_display_on(void);

// 底层 I2C
void bsp_ssd1306_reset(void);
void bsp_ssd1306_write_command(uint8_t byte);
void bsp_ssd1306_write_data(uint8_t *buffer, size_t buff_size);
BspSsd1306Error bsp_ssd1306_fill_buffer(uint8_t *buf, uint32_t len);
void bsp_ssd1306_clear_oled(void);

// printf 到 OLED
void bsp_ssd1306_printf(char *fmt, ...);

#endif // BSP_SSD1306_H
