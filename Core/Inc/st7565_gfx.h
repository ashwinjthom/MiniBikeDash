/*
 * st7565_gfx.h
 *
 *  Created on: Jul 14, 2026
 *      Author: Ashwin Thomas
 */

#ifndef ST7565_GFX_H
#define ST7565_GFX_H

#include <st7565_8080.h>
/* Simple in-RAM framebuffer + text layer for the ST7565 driver.
 *
 * Typical usage in main():
 *
 *   ST7565_Init(0x20);          // once, at startup, sets initial contrast
 *   ...
 *   ST7565_ClearBuffer();
 *   ST7565_DrawNumber(0, 0, 42);
 *   ST7565_Update();            // pushes the framebuffer to the panel
 */

#define ST7565_FONT_WIDTH   5
#define ST7565_FONT_HEIGHT  7

void ST7565_ClearBuffer(void);
void ST7565_Update(void);

void ST7565_DrawPixel(int16_t x, int16_t y, uint8_t color);

void ST7565_DrawChar(int16_t x, int16_t y, char c);
void ST7565_DrawString(int16_t x, int16_t y, const char *s);
void ST7565_DrawNumber(int16_t x, int16_t y, int32_t value);

/* Same as above, but each font pixel is drawn as a scale x scale block of
 * real pixels. scale=1 is identical to the plain functions above (7px tall
 * glyphs); scale=2 gives 14px tall glyphs, scale=4 gives 28px tall glyphs,
 * etc. Remember the panel is only 32px tall, so pick a scale (and y
 * position) that actually fits: scale 4 is about the max for one line. */
void ST7565_DrawCharScaled(int16_t x, int16_t y, char c, uint8_t scale);
void ST7565_DrawStringScaled(int16_t x, int16_t y, const char *s, uint8_t scale);
void ST7565_DrawNumberScaled(int16_t x, int16_t y, int32_t value, uint8_t scale);

/* ---- Basic shape primitives ---------------------------------------- */
void ST7565_DrawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color);
void ST7565_FillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color);

/* ---- Status icons -----------------------------------------------------
 * Both take a bounding box (x, y, w, h) and a charge/fuel level from 0
 * (empty) to 4 (full). Segments are drawn "on" (foreground color); nothing
 * is drawn to clear a background, so call ST7565_ClearBuffer() (or fill
 * the box with color=0 yourself) first if you're redrawing over a
 * previous level.
 * ------------------------------------------------------------------- */

/* Horizontal battery: rounded body + small positive-terminal nub on the
 * right, with up to 4 filled bars inside showing charge level.
 * Minimum usable size is roughly w>=14, h>=8. */
void ST7565_DrawBattery(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t level);

/* Horizontal gas/fuel gauge: plain outlined bar with up to 4 filled
 * segments, no terminal nub. Minimum usable size is roughly w>=10, h>=6. */
void ST7565_DrawGasGauge(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t level);

/* Same as ST7565_DrawGasGauge, but with an 'E' and 'F' label flanking the
 * bar. (x, y) is the top-left of the 'E' label, not the bar itself -- the
 * bar and 'F' are placed to the right of it automatically. Total width
 * drawn is roughly (ST7565_FONT_WIDTH + 2) * 2 + w. */
void ST7565_DrawGasGaugeEF(int16_t x, int16_t y, uint8_t bar_w, uint8_t bar_h, uint8_t level);

#endif /* ST7565_GFX_H */
