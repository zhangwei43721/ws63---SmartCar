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

#include "ssd1306.h"

#include <math.h>
#include <securec.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "i2c.h"
#include "soc_osal.h"

#define CONFIG_I2C_MASTER_BUS_ID 1
#define I2C_SLAVE2_ADDR 0x3C
#define SSD1306_CTRL_CMD            0x00
#define SSD1306_CTRL_DATA           0x40
#define SSD1306_CTRL_MASK_CONT      (0x1 << 7)
#define DOUBLE 2

void bsp_ssd1306_reset(void)
{
    osal_mdelay(1);
}

static uint32_t ssd1306_SendData(uint8_t *buffer, uint32_t size)
{
    uint16_t dev_addr = I2C_SLAVE2_ADDR;
    i2c_data_t data = {0};
    data.send_buf = buffer;
    data.send_len = size;
    uint32_t retval = uapi_i2c_master_write(CONFIG_I2C_MASTER_BUS_ID, dev_addr, &data);
    if (retval != 0) {
        return retval;
    }
    return 0;
}

static uint32_t ssd1306_WriteByte(uint8_t regAddr, uint8_t byte)
{
    uint8_t buffer[] = {regAddr, byte};
    return ssd1306_SendData(buffer, sizeof(buffer));
}

void bsp_ssd1306_write_command(uint8_t byte)
{
    ssd1306_WriteByte(SSD1306_CTRL_CMD, byte);
}

void bsp_ssd1306_write_data(uint8_t *buffer, size_t buff_size)
{
    uint8_t data[SSD1306_WIDTH * DOUBLE] = {0};
    for (uint32_t i = 0; i < buff_size; i++) {
        data[i * DOUBLE] = SSD1306_CTRL_DATA | SSD1306_CTRL_MASK_CONT;
        data[i * DOUBLE + 1] = buffer[i];
    }
    data[(buff_size - 1) * DOUBLE] = SSD1306_CTRL_DATA;
    ssd1306_SendData(data, sizeof(data));
}

static uint8_t g_ssd1306_buffer[SSD1306_BUFFER_SIZE];
static BspSsd1306Ctx g_ssd1306_ctx;

BspSsd1306Error bsp_ssd1306_fill_buffer(uint8_t *buf, uint32_t len)
{
    BspSsd1306Error ret = BSP_SSD1306_ERR;
    if (len <= SSD1306_BUFFER_SIZE) {
        memcpy_s(g_ssd1306_buffer, len + 1, buf, len);
        ret = BSP_SSD1306_OK;
    }
    return ret;
}

static void bsp_ssd1306_init_cmd(void)
{
    bsp_ssd1306_write_command(SSD1306_CMD_ENTIRE_ON_RESUME);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_OFFSET);
    bsp_ssd1306_write_command(0x00);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_CLOCK_DIV);
    bsp_ssd1306_write_command(SSD1306_CLOCK_DIV_F0);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_PRECHARGE);
    bsp_ssd1306_write_command(SSD1306_PRECHARGE_1DCLK);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_COM_PINS);
#if (SSD1306_HEIGHT == 32)
    bsp_ssd1306_write_command(SSD1306_COM_PINS_ALT_NOCFG);
#elif (SSD1306_HEIGHT == 64)
    bsp_ssd1306_write_command(SSD1306_COM_PINS_ALT_CFG);
#elif (SSD1306_HEIGHT == 128)
    bsp_ssd1306_write_command(SSD1306_COM_PINS_ALT_CFG);
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif

    bsp_ssd1306_write_command(SSD1306_CMD_SET_VCOM_DESELECT);
    bsp_ssd1306_write_command(SSD1306_VCOM_83X_VCC);

    bsp_ssd1306_write_command(SSD1306_CMD_CHARGE_PUMP);
    bsp_ssd1306_write_command(SSD1306_CHARGE_PUMP_ENABLE);
    bsp_ssd1306_set_display_on(1);
}

bool bsp_ssd1306_init(void)
{
    uint8_t probe_buf[] = {SSD1306_CTRL_CMD, 0x00};
    i2c_data_t probe_data = {0};
    probe_data.send_buf = probe_buf;
    probe_data.send_len = sizeof(probe_buf);
    uint32_t probe_ret = uapi_i2c_master_write(CONFIG_I2C_MASTER_BUS_ID, I2C_SLAVE2_ADDR, &probe_data);
    if (probe_ret != 0)
        return false;

    bsp_ssd1306_reset();
    bsp_ssd1306_set_display_on(0);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_MEM_ADDR_MODE);
    bsp_ssd1306_write_command(SSD1306_ADDR_MODE_HORIZONTAL);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_PAGE_START);

#ifdef SSD1306_MIRROR_VERT
    bsp_ssd1306_write_command(SSD1306_CMD_SET_COM_SCAN_NORMAL);
#else
    bsp_ssd1306_write_command(SSD1306_CMD_SET_COM_SCAN_REMAP);
#endif

    bsp_ssd1306_write_command(SSD1306_COL_START);
    bsp_ssd1306_write_command(0x10);

    bsp_ssd1306_write_command(SSD1306_CMD_SET_START_LINE);

    bsp_ssd1306_set_contrast(0xFF);

#ifdef SSD1306_MIRROR_HORIZ
    bsp_ssd1306_write_command(SSD1306_CMD_SET_SEG_REMAP_0);
#else
    bsp_ssd1306_write_command(SSD1306_CMD_SET_SEG_REMAP_127);
#endif

#ifdef SSD1306_INVERSE_COLOR
    bsp_ssd1306_write_command(SSD1306_CMD_INVERT_DISPLAY);
#else
    bsp_ssd1306_write_command(SSD1306_CMD_NORMAL_DISPLAY);
#endif

#if (SSD1306_HEIGHT == 128)
    bsp_ssd1306_write_command(0xFF);
#else
    bsp_ssd1306_write_command(SSD1306_CMD_SET_MULTIPLEX);
#endif

#if (SSD1306_HEIGHT == 32)
    bsp_ssd1306_write_command(SSD1306_MUX_32);
#elif (SSD1306_HEIGHT == 64)
    bsp_ssd1306_write_command(SSD1306_MUX_64);
#elif (SSD1306_HEIGHT == 128)
    bsp_ssd1306_write_command(SSD1306_MUX_64);
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif
    bsp_ssd1306_init_cmd();
    bsp_ssd1306_fill(BSP_SSD1306_COLOR_BLACK);
    bsp_ssd1306_update_screen();

    g_ssd1306_ctx.CurrentX = 0;
    g_ssd1306_ctx.CurrentY = 0;
    g_ssd1306_ctx.Initialized = 1;
    return true;
}

void bsp_ssd1306_fill(BspSsd1306Color color)
{
    for (uint32_t i = 0; i < sizeof(g_ssd1306_buffer); i++) {
        g_ssd1306_buffer[i] = (color == BSP_SSD1306_COLOR_BLACK) ? 0x00 : 0xFF;
    }
}

void bsp_ssd1306_update_screen(void)
{
    uint8_t cmd[] = {
        SSD1306_CMD_SET_COL_ADDR,
        SSD1306_COL_START,
        SSD1306_COL_END,
        SSD1306_CMD_SET_PAGE_ADDR,
        SSD1306_PAGE_START,
        SSD1306_PAGE_END,
    };
    uint32_t count = 0;
    static uint8_t data[sizeof(cmd) * DOUBLE + SSD1306_BUFFER_SIZE + 1];
    (void)memset_s(data, sizeof(data), 0, sizeof(data));

    for (uint32_t i = 0; i < sizeof(cmd) / sizeof(cmd[0]); i++) {
        data[count++] = SSD1306_CTRL_CMD | SSD1306_CTRL_MASK_CONT;
        data[count++] = cmd[i];
    }

    data[count++] = SSD1306_CTRL_DATA;
    memcpy_s(&data[count], SSD1306_BUFFER_SIZE + 1, g_ssd1306_buffer, SSD1306_BUFFER_SIZE);
    count += sizeof(g_ssd1306_buffer);

    uint32_t retval = ssd1306_SendData(data, count);
    if (retval != 0) {
        printf("bsp_ssd1306_update_screen send frame data failed: %d!\r\n", retval);
    }
}

void bsp_ssd1306_draw_pixel(uint8_t x, uint8_t y, BspSsd1306Color color)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }
    BspSsd1306Color color1 = color;
    if (g_ssd1306_ctx.Inverted) {
        color1 = (BspSsd1306Color)!color1;
    }

    uint32_t c = 8;
    if (color == BSP_SSD1306_COLOR_WHITE) {
        g_ssd1306_buffer[x + (y / c) * SSD1306_WIDTH] |= 1 << (y % c);
    } else {
        g_ssd1306_buffer[x + (y / c) * SSD1306_WIDTH] &= ~(1 << (y % c));
    }
}

char bsp_ssd1306_draw_char(char ch, FontDef Font, BspSsd1306Color color)
{
    uint32_t ch_min = 32;
    uint32_t ch_max = 126;
    if ((uint32_t)ch < ch_min || (uint32_t)ch > ch_max) {
        return 0;
    }

    if (SSD1306_WIDTH < (g_ssd1306_ctx.CurrentX + Font.FontWidth) ||
        SSD1306_HEIGHT < (g_ssd1306_ctx.CurrentY + Font.FontHeight)) {
        return 0;
    }

    for (uint32_t i = 0; i < Font.FontHeight; i++) {
        uint32_t b = Font.data[(ch - ch_min) * Font.FontHeight + i];
        for (uint32_t j = 0; j < Font.FontWidth; j++) {
            if ((b << j) & 0x8000) {
                bsp_ssd1306_draw_pixel(g_ssd1306_ctx.CurrentX + j, (g_ssd1306_ctx.CurrentY + i),
                                       (BspSsd1306Color)color);
            } else {
                bsp_ssd1306_draw_pixel(g_ssd1306_ctx.CurrentX + j, (g_ssd1306_ctx.CurrentY + i),
                                       (BspSsd1306Color)!color);
            }
        }
    }

    g_ssd1306_ctx.CurrentX += Font.FontWidth;
    return ch;
}

char bsp_ssd1306_draw_string(char *str, FontDef Font, BspSsd1306Color color)
{
    char *str1 = str;
    while (*str1) {
        if (bsp_ssd1306_draw_char(*str1, Font, color) != *str1) {
            return *str1;
        }
        str1++;
    }
    return *str1;
}

void bsp_ssd1306_set_cursor(uint8_t x, uint8_t y)
{
    g_ssd1306_ctx.CurrentX = x;
    g_ssd1306_ctx.CurrentY = y;
}

void bsp_ssd1306_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, BspSsd1306Color color)
{
    uint8_t x = x1;
    uint8_t y = y1;
    int32_t deltaX = abs(x2 - x1);
    int32_t deltaY = abs(y2 - y1);
    int32_t signX = ((x1 < x2) ? 1 : -1);
    int32_t signY = ((y1 < y2) ? 1 : -1);
    int32_t error = deltaX - deltaY;
    int32_t error2;
    bsp_ssd1306_draw_pixel(x2, y2, color);
    while ((x1 != x2) || (y1 != y2)) {
        bsp_ssd1306_draw_pixel(x1, y1, color);
        error2 = error * DOUBLE;
        if (error2 > -deltaY) {
            error -= deltaY;
            x += signX;
        }
        if (error2 < deltaX) {
            error += deltaX;
            y += signY;
        }
    }
}

void bsp_ssd1306_draw_polyline(const BspSsd1306Vertex *par_vertex, uint16_t par_size, BspSsd1306Color color)
{
    if (par_vertex != 0) {
        for (uint16_t i = 1; i < par_size; i++) {
            bsp_ssd1306_draw_line(par_vertex[i - 1].x, par_vertex[i - 1].y,
                                  par_vertex[i].x, par_vertex[i].y, color);
        }
    }
}

void bsp_ssd1306_draw_circle(uint8_t par_x, uint8_t par_y, uint8_t par_r, BspSsd1306Color par_color)
{
    int32_t x = -par_r;
    int32_t y = 0;
    int32_t b = 2;
    int32_t err = b - b * par_r;
    int32_t e2;

    if (par_x >= SSD1306_WIDTH || par_y >= SSD1306_HEIGHT) {
        return;
    }

    do {
        bsp_ssd1306_draw_pixel(par_x - x, par_y + y, par_color);
        bsp_ssd1306_draw_pixel(par_x + x, par_y + y, par_color);
        bsp_ssd1306_draw_pixel(par_x + x, par_y - y, par_color);
        bsp_ssd1306_draw_pixel(par_x - x, par_y - y, par_color);
        e2 = err;
        if (e2 <= y) {
            y++;
            err = err + (y * b + 1);
            if (-x == y && e2 <= x) {
                e2 = 0;
            }
        }
        if (e2 > x) {
            x++;
            err = err + (x * b + 1);
        }
    } while (x <= 0);
}

void bsp_ssd1306_draw_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, BspSsd1306Color color)
{
    bsp_ssd1306_draw_line(x1, y1, x2, y1, color);
    bsp_ssd1306_draw_line(x2, y1, x2, y2, color);
    bsp_ssd1306_draw_line(x2, y2, x1, y2, color);
    bsp_ssd1306_draw_line(x1, y2, x1, y1, color);
}

void bsp_ssd1306_draw_bitmap(const uint8_t *bitmap, uint32_t size)
{
    unsigned int c = 8;
    uint8_t rows = size * c / SSD1306_WIDTH;
    if (rows > SSD1306_HEIGHT) {
        rows = SSD1306_HEIGHT;
    }
    for (uint8_t y = 0; y < rows; y++) {
        for (uint8_t x = 0; x < SSD1306_WIDTH; x++) {
            uint8_t byte = bitmap[(y * SSD1306_WIDTH / c) + (x / c)];
            uint8_t bit = byte & (0x80 >> (x % c));
            bsp_ssd1306_draw_pixel(x, y, bit ? BSP_SSD1306_COLOR_WHITE : BSP_SSD1306_COLOR_BLACK);
        }
    }
}

void bsp_ssd1306_draw_region(uint8_t x, uint8_t y, uint8_t w, const uint8_t *data, uint32_t size)
{
    uint32_t stride = w;
    uint8_t h = w;
    uint8_t width = w;
    if (x + w > SSD1306_WIDTH || y + h > SSD1306_HEIGHT || w * h == 0) {
        printf("%dx%d @ %d,%d out of range or invalid!\r\n", w, h, x, y);
        return;
    }

    width = (width <= SSD1306_WIDTH ? width : SSD1306_WIDTH);
    h = (h <= SSD1306_HEIGHT ? h : SSD1306_HEIGHT);
    stride = (stride == 0 ? w : stride);
    unsigned int c = 8;

    uint8_t rows = size * c / stride;
    for (uint8_t i = 0; i < rows; i++) {
        uint32_t base = i * stride / c;
        for (uint8_t j = 0; j < width; j++) {
            uint32_t idx = base + (j / c);
            uint8_t byte = idx < size ? data[idx] : 0;
            uint8_t bit = byte & (0x80 >> (j % c));
            bsp_ssd1306_draw_pixel(x + j, y + i, bit ? BSP_SSD1306_COLOR_WHITE : BSP_SSD1306_COLOR_BLACK);
        }
    }
}

void bsp_ssd1306_set_contrast(uint8_t value)
{
    bsp_ssd1306_write_command(SSD1306_CMD_SET_CONTRAST);
    bsp_ssd1306_write_command(value);
}

void bsp_ssd1306_set_display_on(uint8_t on)
{
    uint8_t value;
    if (on) {
        value = SSD1306_CMD_DISPLAY_ON;
        g_ssd1306_ctx.DisplayOn = 1;
    } else {
        value = SSD1306_CMD_DISPLAY_OFF;
        g_ssd1306_ctx.DisplayOn = 0;
    }
    bsp_ssd1306_write_command(value);
}

uint8_t bsp_ssd1306_get_display_on(void)
{
    return g_ssd1306_ctx.DisplayOn;
}

static int g_ssd1306_current_loc_v = 0;
#define SSD1306_INTERVAL_V (15)

void bsp_ssd1306_clear_oled(void)
{
    bsp_ssd1306_fill(BSP_SSD1306_COLOR_BLACK);
    g_ssd1306_current_loc_v = 0;
}

void bsp_ssd1306_printf(char *fmt, ...)
{
    char buffer[20];
    if (fmt) {
        va_list argList;
        va_start(argList, fmt);
        int ret = vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer), fmt, argList);
        if (ret < 0) {
            printf("buffer is null\r\n");
        }
        va_end(argList);
        bsp_ssd1306_set_cursor(0, g_ssd1306_current_loc_v);
        bsp_ssd1306_draw_string(buffer, Font_7x10, BSP_SSD1306_COLOR_WHITE);
        bsp_ssd1306_update_screen();
    }
    g_ssd1306_current_loc_v += SSD1306_INTERVAL_V;
}
