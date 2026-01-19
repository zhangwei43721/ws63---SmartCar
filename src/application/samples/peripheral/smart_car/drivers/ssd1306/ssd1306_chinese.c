#include "ssd1306.h"
#include "FontDotMatrix16.h"
#include "ssd1306_fonts.h"
#include <string.h>

/* Helper to get UTF-8 character length */
static int GetUtf8CharLength(const char *target)
{
    unsigned char c = (unsigned char)target[0];
    if (c <= 0x7F) return 1;
    if ((c >= 0xC2 && c <= 0xDF)) return 2;
    if ((c >= 0xE0 && c <= 0xEF)) return 3;
    if ((c >= 0xF0 && c <= 0xF7)) return 4;
    return 1;
}

/* Find index in the font array */
static int FindFontIndex(const char *target, int len)
{
    int i;
    int count = 162; // Based on the array size in FontDotMatrix16.c
    
    for (i = 0; i < count; i++) {
        const char *idx_str = g_font_dot_matrix_16_index[i];
        if (strncmp(idx_str, target, len) == 0 && idx_str[len] == '\0') {
            return i;
        }
    }
    return -1;
}

void ssd1306_DrawString16(uint8_t x, uint8_t y, const char *str, SSD1306_COLOR color)
{
    uint8_t curr_x = x;
    uint8_t curr_y = y;
    const char *p = str;
    
    while (*p) {
        int len = GetUtf8CharLength(p);
        
        if (len == 1) {
            // ASCII character
            // Center 7x10 font in 16px height: offset = (16-10)/2 = 3
            ssd1306_SetCursor(curr_x, curr_y + 3);
            ssd1306_DrawChar(*p, Font_7x10, color);
            curr_x += 7; // Font width
        } else {
            int index = FindFontIndex(p, len);
            
            if (index >= 0) {
                // Draw 16x16 bitmap
                const char *bitmap = g_font_dot_matrix_16[index];
                for (int i = 0; i < 16; i++) {
                    uint8_t byte = bitmap[i];
                    for (int b = 0; b < 8; b++) {
                        ssd1306_DrawPixel(curr_x + i, curr_y + b, (byte & (1 << b)) ? color : (SSD1306_COLOR)!color);
                    }
                    byte = bitmap[i + 16];
                    for (int b = 0; b < 8; b++) {
                        ssd1306_DrawPixel(curr_x + i, curr_y + 8 + b, (byte & (1 << b)) ? color : (SSD1306_COLOR)!color);
                    }
                }
                curr_x += 16;
            } else {
                curr_x += 16; 
            }
        }
        
        p += len;
        
        if (curr_x + 16 > SSD1306_WIDTH) {
            curr_x = 0;
            curr_y += 16;
        }
    }
}
