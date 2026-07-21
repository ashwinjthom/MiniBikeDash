#include "st7565_gfx.h"
#include <stdio.h>

static uint8_t s_framebuffer[ST7565_WIDTH * ST7565_PAGES];

/* Minimal 5x7 font: digits, a few punctuation marks, and space. Each glyph
 * is 5 bytes (one per column), bit 0 = top row, bit 6 = bottom row. Add
 * more entries here (letters, etc.) the same way if you need them later. */
typedef struct { char ch; uint8_t col[ST7565_FONT_WIDTH]; } font_glyph_t;

static const font_glyph_t s_font[] = {
    { ' ', {0x00,0x00,0x00,0x00,0x00} },
    { '-', {0x08,0x08,0x08,0x08,0x08} },
    { '.', {0x00,0x60,0x60,0x00,0x00} },
    { ':', {0x00,0x36,0x36,0x00,0x00} },
    { '0', {0x3E,0x51,0x49,0x45,0x3E} },
    { '1', {0x00,0x42,0x7F,0x40,0x00} },
    { '2', {0x42,0x61,0x51,0x49,0x46} },
    { '3', {0x21,0x41,0x45,0x4B,0x31} },
    { '4', {0x18,0x14,0x12,0x7F,0x10} },
    { '5', {0x27,0x45,0x45,0x45,0x39} },
    { '6', {0x3C,0x4A,0x49,0x49,0x30} },
    { '7', {0x01,0x71,0x09,0x05,0x03} },
    { '8', {0x36,0x49,0x49,0x49,0x36} },
    { '9', {0x06,0x49,0x49,0x29,0x1E} },
	{ 'E', {0x7F,0x49,0x49,0x49,0x41} },
	{ 'F', {0x7F,0x09,0x09,0x09,0x01} },
};
#define FONT_COUNT (sizeof(s_font) / sizeof(s_font[0]))

static const uint8_t *ST7565_GetGlyph(char c)
{
    for (uint32_t i = 0; i < FONT_COUNT; i++)
    {
        if (s_font[i].ch == c)
            return s_font[i].col;
    }
    return NULL; /* unsupported character -> drawn as blank */
}

void ST7565_ClearBuffer(void)
{
    for (uint32_t i = 0; i < sizeof(s_framebuffer); i++)
        s_framebuffer[i] = 0x00;
}

void ST7565_Update(void)
{
    ST7565_WriteBuffer(s_framebuffer, sizeof(s_framebuffer));
}

void ST7565_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= (int16_t)ST7565_WIDTH || y < 0 || y >= (int16_t)ST7565_HEIGHT)
        return;

    uint32_t idx = ((uint32_t)y / 8) * ST7565_WIDTH + (uint32_t)x;
    uint8_t bit = (uint8_t)(1u << (y % 8));

    if (color)
        s_framebuffer[idx] |= bit;
    else
        s_framebuffer[idx] &= (uint8_t)~bit;
}

void ST7565_DrawChar(int16_t x, int16_t y, char c)
{
    ST7565_DrawCharScaled(x, y, c, 1);
}

void ST7565_DrawCharScaled(int16_t x, int16_t y, char c, uint8_t scale)
{
    if (scale == 0)
        scale = 1;

    const uint8_t *glyph = ST7565_GetGlyph(c);
    if (!glyph)
        return; /* leave blank for characters not in the font table */

    for (uint8_t col = 0; col < ST7565_FONT_WIDTH; col++)
    {
        uint8_t line = glyph[col];
        for (uint8_t row = 0; row < ST7565_FONT_HEIGHT; row++)
        {
            uint8_t pixel_on = (line >> row) & 0x01;

            /* Blow up this one font pixel into a scale x scale block. */
            for (uint8_t sy = 0; sy < scale; sy++)
            {
                for (uint8_t sx = 0; sx < scale; sx++)
                {
                    ST7565_DrawPixel((int16_t)(x + col * scale + sx),
                                      (int16_t)(y + row * scale + sy),
                                      pixel_on);
                }
            }
        }
    }
}

void ST7565_DrawString(int16_t x, int16_t y, const char *s)
{
    ST7565_DrawStringScaled(x, y, s, 1);
}

void ST7565_DrawStringScaled(int16_t x, int16_t y, const char *s, uint8_t scale)
{
    if (scale == 0)
        scale = 1;

    int16_t advance = (int16_t)((ST7565_FONT_WIDTH + 1) * scale); /* incl. spacing */
    while (*s)
    {
        ST7565_DrawCharScaled(x, y, *s++, scale);
        x = (int16_t)(x + advance);
    }
}

void ST7565_DrawNumber(int16_t x, int16_t y, int32_t value)
{
    ST7565_DrawNumberScaled(x, y, value, 1);
}

void ST7565_DrawNumberScaled(int16_t x, int16_t y, int32_t value, uint8_t scale)
{
    char buf[12]; /* enough for -2147483648 + NUL */
    snprintf(buf, sizeof(buf), "%ld", (long)value);
    ST7565_DrawStringScaled(x, y, buf, scale);
}

/* ---------------------------------------------------------------------- */

void ST7565_DrawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color)
{
    if (w == 0 || h == 0)
        return;

    for (uint8_t i = 0; i < w; i++)
    {
        ST7565_DrawPixel((int16_t)(x + i), y, color);
        ST7565_DrawPixel((int16_t)(x + i), (int16_t)(y + h - 1), color);
    }
    for (uint8_t i = 0; i < h; i++)
    {
        ST7565_DrawPixel(x, (int16_t)(y + i), color);
        ST7565_DrawPixel((int16_t)(x + w - 1), (int16_t)(y + i), color);
    }
}

void ST7565_FillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color)
{
    for (uint8_t i = 0; i < w; i++)
        for (uint8_t j = 0; j < h; j++)
            ST7565_DrawPixel((int16_t)(x + i), (int16_t)(y + j), color);
}

/* Fills up to max_level 1px-separated segments left-to-right across a
 * (x, y, w, h) area, 'level' of them turned on. Shared by the battery and
 * gas gauge icons below. */
static void ST7565_FillLevelSegments(int16_t x, int16_t y, uint8_t w, uint8_t h,
                                      uint8_t level, uint8_t max_level)
{
    if (max_level == 0)
        return;
    if (level > max_level)
        level = max_level;

    uint8_t gap = 1;
    uint8_t gaps_total = (uint8_t)(gap * (max_level - 1));
    if (w <= gaps_total)
        return; /* box too small to fit max_level segments */

    uint8_t seg_w = (uint8_t)((w - gaps_total) / max_level);
    if (seg_w == 0)
        return;

    for (uint8_t i = 0; i < level; i++)
    {
        int16_t seg_x = (int16_t)(x + i * (seg_w + gap));
        ST7565_FillRect(seg_x, y, seg_w, h, 1);
    }
}

void ST7565_DrawBattery(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t level)
{
    const uint8_t nub_w = 2;
    const int16_t inset = 2;

    if (w < nub_w + 6 || h < 6)
        return; /* too small to draw meaningfully */

    uint8_t body_w = (uint8_t)(w - nub_w);
    uint8_t nub_h  = (uint8_t)(h / 2);
    int16_t nub_y  = (int16_t)(y + (h - nub_h) / 2);

    ST7565_DrawRect(x, y, body_w, h, 1);
    ST7565_FillRect((int16_t)(x + body_w), nub_y, nub_w, nub_h, 1);

    ST7565_FillLevelSegments((int16_t)(x + inset), (int16_t)(y + inset),
                              (uint8_t)(body_w - 2 * inset),
                              (uint8_t)(h - 2 * inset),
                              level, 5);
}

void ST7565_DrawGasGauge(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t level)
{
    const int16_t inset = 2;

    if (w < 10 || h < 6)
        return; /* too small to draw meaningfully */

    ST7565_DrawRect(x, y, w, h, 1);

    ST7565_FillLevelSegments((int16_t)(x + inset), (int16_t)(y + inset),
                              (uint8_t)(w - 2 * inset),
                              (uint8_t)(h - 2 * inset),
                              level, 4);
}

void ST7565_DrawGasGaugeEF(int16_t x, int16_t y, uint8_t bar_w, uint8_t bar_h, uint8_t level)
{
    const int16_t label_gap = 2;
    /* vertically center the 7px-tall letters on the bar */
    int16_t label_y = (int16_t)(y + (bar_h / 2) - (ST7565_FONT_HEIGHT / 2));

    ST7565_DrawChar(x, label_y, 'E');

    int16_t bar_x = (int16_t)(x + ST7565_FONT_WIDTH + label_gap);
    ST7565_DrawGasGauge(bar_x, y, bar_w, bar_h, level);

    int16_t f_x = (int16_t)(bar_x + bar_w + label_gap);
    ST7565_DrawChar(f_x, label_y, 'F');
}
