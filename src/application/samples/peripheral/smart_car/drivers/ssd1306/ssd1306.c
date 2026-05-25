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
#define SSD1306_CTRL_CMD 0x00
#define SSD1306_CTRL_DATA 0x40
#define SSD1306_MASK_CONT (0x1 << 7)
#define DOUBLE 2

void ssd1306_Reset(void)
{
    // 等待屏幕启动，1ms。此处的延时非常重要
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
        // 移除错误打印，避免刷屏
        // printf("I2cWrite(%02X) failed, %0X!\n", data.send_buf[1], retval);
        return retval;
    }
    return 0;
}

static uint32_t ssd1306_WriteByte(uint8_t regAddr, uint8_t byte)
{
    uint8_t buffer[] = {regAddr, byte};
    return ssd1306_SendData(buffer, sizeof(buffer));
}

// 向命令寄存器发送一个字节
void ssd1306_WriteCommand(uint8_t byte)
{
    ssd1306_WriteByte(SSD1306_CTRL_CMD, byte);
}

// 发送数据
void ssd1306_WriteData(uint8_t *buffer, uint32_t buff_size)
{
    uint8_t data[SSD1306_WIDTH * DOUBLE] = {0};
    for (uint32_t i = 0; i < buff_size; i++) {
        data[i * DOUBLE] = SSD1306_CTRL_DATA | SSD1306_MASK_CONT;
        data[i * DOUBLE + 1] = buffer[i];
    }
    data[(buff_size - 1) * DOUBLE] = SSD1306_CTRL_DATA;
    ssd1306_SendData(data, sizeof(data));
}

// 屏幕缓冲区
static uint8_t SSD1306_Buffer[SSD1306_BUFFER_SIZE];

// 屏幕对象
static SSD1306_t SSD1306;

// 用给定缓冲区的数据填充屏幕缓冲区
SSD1306_Error_t ssd1306_FillBuffer(uint8_t *buf, uint32_t len)
{
    SSD1306_Error_t ret = SSD1306_ERR;
    if (len <= SSD1306_BUFFER_SIZE) {
        memcpy_s(SSD1306_Buffer, len + 1, buf, len);
        ret = SSD1306_OK;
    }
    return ret;
}

void ssd1306_Init_CMD(void)
{
    ssd1306_WriteCommand(0xA4); // 0xa4,输出跟随RAM内容;0xa5,输出忽略RAM内容

    ssd1306_WriteCommand(0xD3); // -设置显示偏移 - 校验
    ssd1306_WriteCommand(0x00); // -无偏移

    ssd1306_WriteCommand(0xD5); // --设置显示时钟分频比/振荡器频率
    ssd1306_WriteCommand(0xF0); // --设置分频比

    ssd1306_WriteCommand(0xD9); // --设置预充电周期
    ssd1306_WriteCommand(0x11); // 默认为0x22

    ssd1306_WriteCommand(0xDA); // --设置COM引脚硬件配置 - 校验
#if (SSD1306_HEIGHT == 32)
    ssd1306_WriteCommand(0x02);
#elif (SSD1306_HEIGHT == 64)
    ssd1306_WriteCommand(0x12);
#elif (SSD1306_HEIGHT == 128)
    ssd1306_WriteCommand(0x12);
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif

    ssd1306_WriteCommand(0xDB); // --设置VCOMH
    ssd1306_WriteCommand(0x30); // 0x20对应0.77xVcc, 0x30对应0.83xVcc

    ssd1306_WriteCommand(0x8D); // --设置DC-DC使能
    ssd1306_WriteCommand(0x14); //
    ssd1306_SetDisplayOn(1);    // --打开SSD1306面板
}

// 初始化OLED屏幕
bool ssd1306_Init(void)
{
    // 尝试探测设备是否存在（发送一个简单命令测试）
    uint8_t probe_buf[] = {SSD1306_CTRL_CMD, 0x00}; // 发送 NOP 命令测试
    i2c_data_t probe_data = {0};
    probe_data.send_buf = probe_buf;
    probe_data.send_len = sizeof(probe_buf);
    uint32_t probe_ret = uapi_i2c_master_write(CONFIG_I2C_MASTER_BUS_ID, I2C_SLAVE2_ADDR, &probe_data);
    if (probe_ret != 0)
        return false; // 设备探测失败，OLED 不存在或通信失败

    // 复位OLED
    ssd1306_Reset();
    // 初始化OLED
    ssd1306_SetDisplayOn(0); // 关闭显示

    ssd1306_WriteCommand(0x20); // 设置存储器寻址模式
    ssd1306_WriteCommand(0x00); // 00b,水平寻址模式; 01b,垂直寻址模式;
                                // 10b,页寻址模式(复位值); 11b,无效

    ssd1306_WriteCommand(0xB0); // 设置页寻址模式的页起始地址,0-7

#ifdef SSD1306_MIRROR_VERT
    ssd1306_WriteCommand(0xC0); // 垂直镜像
#else
    ssd1306_WriteCommand(0xC8); // 设置COM输出扫描方向
#endif

    ssd1306_WriteCommand(0x00); // ---设置低列地址
    ssd1306_WriteCommand(0x10); // ---设置高列地址

    ssd1306_WriteCommand(0x40); // --设置起始行地址 - 校验

    ssd1306_SetContrast(0xFF);

#ifdef SSD1306_MIRROR_HORIZ
    ssd1306_WriteCommand(0xA0); // 水平镜像
#else
    ssd1306_WriteCommand(0xA1); // --设置段重映射0到127 - 校验
#endif

#ifdef SSD1306_INVERSE_COLOR
    ssd1306_WriteCommand(0xA7); // --设置反色
#else
    ssd1306_WriteCommand(0xA6); // --设置正常颜色
#endif

// 设置多路复用率
#if (SSD1306_HEIGHT == 128)
    // 在SH1106的Luma Python库中发现
    ssd1306_WriteCommand(0xFF);
#else
    ssd1306_WriteCommand(0xA8); // --设置多路复用率(1到64) - 校验
#endif

#if (SSD1306_HEIGHT == 32)
    ssd1306_WriteCommand(0x1F); //
#elif (SSD1306_HEIGHT == 64)
    ssd1306_WriteCommand(0x3F); //
#elif (SSD1306_HEIGHT == 128)
    ssd1306_WriteCommand(0x3F); // 对128像素高的显示屏也有效
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif
    ssd1306_Init_CMD();
    // 清屏
    ssd1306_Fill(Black);

    // 将缓冲区刷新到屏幕
    ssd1306_UpdateScreen();

    // 设置屏幕对象的默认值
    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;

    SSD1306.Initialized = 1;
    return true; // 初始化成功
}

// 用给定颜色填充整个屏幕
void ssd1306_Fill(SSD1306_COLOR color)
{
    // 设置存储器
    uint32_t i;

    for (i = 0; i < sizeof(SSD1306_Buffer); i++) {
        SSD1306_Buffer[i] = (color == Black) ? 0x00 : 0xFF;
    }
}

// 将修改后的屏幕缓冲区写入屏幕
void ssd1306_UpdateScreen(void)
{
    // 向RAM的每一页写入数据。页数
    // 取决于屏幕高度：
    //
    // * 32像素  ==  4页
    // * 64像素  ==  8页
    // * 128像素 ==  16页

    uint8_t cmd[] = {
        0X21, // 设置列起始和结束地址
        0X00, // 列起始地址 0
        0X7F, // 列终止地址 127
        0X22, // 设置页起始和结束地址
        0X00, // 页起始地址 0
        0X07, // 页终止地址 7
    };
    uint32_t count = 0;
    uint8_t data[sizeof(cmd) * DOUBLE + SSD1306_BUFFER_SIZE + 1] = {};

    // 复制命令
    for (uint32_t i = 0; i < sizeof(cmd) / sizeof(cmd[0]); i++) {
        data[count++] = SSD1306_CTRL_CMD | SSD1306_MASK_CONT;
        data[count++] = cmd[i];
    }

    // 复制帧数据
    data[count++] = SSD1306_CTRL_DATA;
    memcpy_s(&data[count], SSD1306_BUFFER_SIZE + 1, SSD1306_Buffer, SSD1306_BUFFER_SIZE);
    count += sizeof(SSD1306_Buffer);

    // 发送到I2C总线
    uint32_t retval = ssd1306_SendData(data, count);
    if (retval != 0) {
        printf("ssd1306_UpdateScreen send frame data filed: %d!\r\n", retval);
    }
}

// 在屏幕缓冲区中绘制一个像素
// X => X坐标
// Y => Y坐标
// color => 像素颜色
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        // 不要写入缓冲区外部
        return;
    }
    SSD1306_COLOR color1 = color;
    // 检查像素是否应反转
    if (SSD1306.Inverted) {
        color1 = (SSD1306_COLOR)!color1;
    }

    // 以正确的颜色绘制
    uint32_t c = 8; // 8
    if (color == White) {
        SSD1306_Buffer[x + (y / c) * SSD1306_WIDTH] |= 1 << (y % c);
    } else {
        SSD1306_Buffer[x + (y / c) * SSD1306_WIDTH] &= ~(1 << (y % c));
    }
}

// 在屏幕缓冲区绘制1个字符
// ch       => 要写入的字符
// Font     => 用于写入的字体
// color    => 黑或白
char ssd1306_DrawChar(char ch, FontDef Font, SSD1306_COLOR color)
{
    uint32_t i, b, j;

    // 检查字符是否有效
    uint32_t ch_min = 32;  // 32
    uint32_t ch_max = 126; // 126
    if ((uint32_t)ch < ch_min || (uint32_t)ch > ch_max) {
        return 0;
    }

    // 检查当前行剩余空间
    if (SSD1306_WIDTH < (SSD1306.CurrentX + Font.FontWidth) || SSD1306_HEIGHT < (SSD1306.CurrentY + Font.FontHeight)) {
        // 当前行空间不足
        return 0;
    }

    // 使用字体写入
    for (i = 0; i < Font.FontHeight; i++) {
        b = Font.data[(ch - ch_min) * Font.FontHeight + i];
        for (j = 0; j < Font.FontWidth; j++) {
            if ((b << j) & 0x8000) {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR)color);
            } else {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR)!color);
            }
        }
    }

    // 当前空间已被占用
    SSD1306.CurrentX += Font.FontWidth;

    // 返回写入的字符以用于校验
    return ch;
}

// 将完整字符串写入屏幕缓冲区
char ssd1306_DrawString(char *str, FontDef Font, SSD1306_COLOR color)
{
    // 写入直到空字节
    char *str1 = str;
    while (*str1) {
        if (ssd1306_DrawChar(*str1, Font, color) != *str1) {
            // 字符无法写入
            return *str1;
        }
        // 下一个字符
        str1++;
    }

    // 一切正常
    return *str1;
}

// 定位光标
void ssd1306_SetCursor(uint8_t x, uint8_t y)
{
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

// 使用Bresenham算法画线
void ssd1306_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color)
{
    uint8_t x = x1;
    uint8_t y = y1;
    int32_t deltaX = abs(x2 - x1);
    int32_t deltaY = abs(y2 - y1);
    int32_t signX = ((x1 < x2) ? 1 : -1);
    int32_t signY = ((y1 < y2) ? 1 : -1);
    int32_t error = deltaX - deltaY;
    int32_t error2;
    ssd1306_DrawPixel(x2, y2, color);
    while ((x1 != x2) || (y1 != y2)) {
        ssd1306_DrawPixel(x1, y1, color);
        error2 = error * DOUBLE;
        if (error2 > -deltaY) {
            error -= deltaY;
            x += signX;
        } else {
            // 无需操作
        }
        if (error2 < deltaX) {
            error += deltaX;
            y += signY;
        } else {
            // 无需操作
        }
    }
}

// 绘制折线
void ssd1306_DrawPolyline(const SSD1306_VERTEX *par_vertex, uint16_t par_size, SSD1306_COLOR color)
{
    uint16_t i;
    if (par_vertex != 0) {
        for (i = 1; i < par_size; i++) {
            ssd1306_DrawLine(par_vertex[i - 1].x, par_vertex[i - 1].y, par_vertex[i].x, par_vertex[i].y, color);
        }
    } else {
        // 无需操作
    }
    return;
}

// 使用Bresenham算法画圆
void ssd1306_DrawCircle(uint8_t par_x, uint8_t par_y, uint8_t par_r, SSD1306_COLOR par_color)
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
        ssd1306_DrawPixel(par_x - x, par_y + y, par_color);
        ssd1306_DrawPixel(par_x + x, par_y + y, par_color);
        ssd1306_DrawPixel(par_x + x, par_y - y, par_color);
        ssd1306_DrawPixel(par_x - x, par_y - y, par_color);
        e2 = err;
        if (e2 <= y) {
            y++;
            err = err + (y * b + 1);
            if (-x == y && e2 <= x) {
                e2 = 0;
            } else {
                // 无需操作
            }
        } else {
            // 无需操作
        }
        if (e2 > x) {
            x++;
            err = err + (x * b + 1);
        } else {
            // 无需操作
        }
    } while (x <= 0);

    return;
}

// 绘制矩形
void ssd1306_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color)
{
    ssd1306_DrawLine(x1, y1, x2, y1, color);
    ssd1306_DrawLine(x2, y1, x2, y2, color);
    ssd1306_DrawLine(x2, y2, x1, y2, color);
    ssd1306_DrawLine(x1, y2, x1, y1, color);
}

void ssd1306_DrawBitmap(const uint8_t *bitmap, uint32_t size)
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
            ssd1306_DrawPixel(x, y, bit ? White : Black);
        }
    }
}

void ssd1306_DrawRegion(uint8_t x, uint8_t y, uint8_t w, const uint8_t *data, uint32_t size)
{
    uint32_t stride = w;
    uint8_t h = w; // 字体宽高一样
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
            ssd1306_DrawPixel(x + j, y + i, bit ? White : Black);
        }
    }
}

void ssd1306_SetContrast(const uint8_t value)
{
    const uint8_t kSetContrastControlRegister = 0x81;
    ssd1306_WriteCommand(kSetContrastControlRegister);
    ssd1306_WriteCommand(value);
}

void ssd1306_SetDisplayOn(const uint8_t on)
{
    uint8_t value;
    if (on) {
        value = 0xAF; // 打开显示
        SSD1306.DisplayOn = 1;
    } else {
        value = 0xAE; // 关闭显示
        SSD1306.DisplayOn = 0;
    }
    ssd1306_WriteCommand(value);
}

uint8_t ssd1306_GetDisplayOn(void)
{
    return SSD1306.DisplayOn;
}

int g_ssd1306_current_loc_v = 0;
#define SSD1306_INTERVAL_V (15)

void ssd1306_ClearOLED(void)
{
    ssd1306_Fill(Black);
    g_ssd1306_current_loc_v = 0;
}

void ssd1306_printf(char *fmt, ...)
{
    char buffer[20];
    int ret = 0;
    if (fmt) {
        va_list argList;
        va_start(argList, fmt);
        ret = vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer), fmt, argList);
        if (ret < 0) {
            printf("buffer is null\r\n");
        }
        va_end(argList);
        ssd1306_SetCursor(0, g_ssd1306_current_loc_v);
        ssd1306_DrawString(buffer, Font_7x10, White);

        ssd1306_UpdateScreen();
    }
    g_ssd1306_current_loc_v += SSD1306_INTERVAL_V;
}
