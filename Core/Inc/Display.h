/*
 * Display.h
 *
 *  Created on: Jul 14, 2026
 *      Author: atdin
 */

#ifndef INC_DISPLAY_H_
#define INC_DISPLAY_H_

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stddef.h>

/* ==========================================================================
 * ST7565R 8080 parallel-interface driver
 * Target: STM32G431CBTx (LQFP48), for the ERC12832-1 (128x32) COG module
 *
 * Pinout, taken from the MCU pinout view:
 *
 *   Data bus (NOTE: bit-reversed across the port):
 *     DB7 = PA0     DB6 = PA1     DB5 = PA2     DB4 = PA3
 *     DB3 = PA4     DB2 = PA5     DB1 = PA6     DB0 = PA7
 *
 *   Control:
 *     /WR (R/W) = PB0   active-low write strobe, data latched on rising edge
 *     A0        = PB1   0 = command, 1 = display data
 *     /CS1      = PB2   active-low chip select
 *
 * Signals the ST7565R needs that are NOT wired to the MCU in this pinout,
 * and therefore must be fixed in hardware on the board:
 *     CS2    -> tie HIGH (VDD)   (chip select is CS1='L' AND CS2='H')
 *     C86    -> tie LOW  (VSS)   (selects 8080-family timing, not 6800)
 *     /RD(E) -> tie HIGH (VDD)   (this driver is write-only, so /RD stays
 *                                 permanently inactive)
 *     /RES   -> pulse low at power-up. Either:
 *                 a) wire it to a spare GPIO and set ST7565_USE_RESET_PIN=1
 *                    below, or
 *                 b) drive it from an external RC power-on-reset circuit and
 *                    leave ST7565_USE_RESET_PIN=0 -- ST7565_HardReset() will
 *                    then just wait for that circuit to release reset.
 * ========================================================================*/

#define ST7565_DATA_PORT       GPIOA
#define ST7565_CTRL_PORT       GPIOB

#define ST7565_WR_PIN          GPIO_PIN_0      /* PB0 */
#define ST7565_A0_PIN          GPIO_PIN_1      /* PB1 */
#define ST7565_CS_PIN          GPIO_PIN_2      /* PB2 */

/* Set to 1 and pick a free pin if /RES is actually wired to the MCU. */
#define ST7565_USE_RESET_PIN   0
#if ST7565_USE_RESET_PIN
#define ST7565_RES_PORT        GPIOB
#define ST7565_RES_PIN         GPIO_PIN_4      /* e.g. PB4, currently unused */
#endif

/* Panel geometry for the ERC12832-1 (128 x 32, i.e. 4 pages of 8 rows) */
#define ST7565_WIDTH            128u
#define ST7565_HEIGHT           32u
#define ST7565_PAGES            (ST7565_HEIGHT / 8u)

/* ---- Low level bus ----------------------------------------------------- */
void ST7565_BusInit(void);
void ST7565_WriteCommand(uint8_t cmd);
void ST7565_WriteData(uint8_t data);
void ST7565_HardReset(void);

/* ---- Command helpers ---------------------------------------------------- */
void ST7565_Init(uint8_t contrast6bit);
void ST7565_DisplayOn(uint8_t on);
void ST7565_SetStartLine(uint8_t line);
void ST7565_SetPage(uint8_t page);
void ST7565_SetColumn(uint8_t col);
void ST7565_SetContrast(uint8_t value6bit);

/* ---- Framebuffer helper -------------------------------------------------
 * Writes 'len' bytes from buf out to the display starting at page 0, col 0,
 * wrapping to the next page every 128 bytes. For a full-screen 128x32
 * update, buf should be ST7565_WIDTH * ST7565_PAGES = 512 bytes.
 * ------------------------------------------------------------------------*/
void ST7565_WriteBuffer(const uint8_t *buf, uint32_t len);


#endif /* INC_DISPLAY_H_ */
