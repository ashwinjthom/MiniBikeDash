// Author: Ashwin Thomas

#include "st7565_8080.h"

/* ---------------------------------------------------------------------------
 * Small delay used to respect the ST7565R's tCYC8/tDS8/tCCLW timing
 * (min ~240 ns cycle, ~40 ns data setup, ~80 ns WR pulse width @VDD=3.3V,
 * see datasheet Table 24). This is computed from SystemCoreClock in
 * ST7565_BusInit() rather than hardcoded, so it stays correct if you change
 * the MCU clock speed later -- a fixed NOP count tuned for one clock speed
 * silently becomes too short (timing violation) if you speed the clock up,
 * or unnecessarily slow if you slow it down.
 * -------------------------------------------------------------------------*/
/* Hardcoded for a 144 MHz core clock: 14 cycles ~= 97 ns per delay phase,
 * comfortably above the ST7565R's tCCLW (80 ns WR pulse width) and tDS8
 * (40 ns data setup) minimums at VDD=3.3V. If you change SystemCoreClock,
 * recompute this as: cycles = (SystemCoreClock_Hz / 1000000) * 100 / 1000
 * (target ~100 ns per phase), or switch back to computing it at runtime in
 * ST7565_BusInit(). */
static uint32_t s_strobe_delay_cycles = 14;

static inline void ST7565_DelayCycles(uint32_t cycles)
{
    while (cycles--)
    {
        __NOP();
    }
}

/* Push one byte onto PA0..PA7. The bus is bit-reversed (DBn = PA(7-n)), so
 * we bit-reverse the byte with the Cortex-M4 RBIT instruction and write the
 * low 8 bits of GPIOA atomically via BSRR, leaving PA8..PA15 untouched. */
static inline void ST7565_SetDataBus(uint8_t data)
{
    uint32_t reversed = __RBIT((uint32_t)data) >> 24; /* bits [7:0] only */
    ST7565_DATA_PORT->BSRR = reversed | ((uint32_t)(~reversed & 0xFFu) << 16);
}

void ST7565_BusInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Data bus: PA0-PA7, push-pull output, idle state doesn't matter but
     * start at 0. */
    ST7565_DATA_PORT->BSRR = 0x00FFu << 16; /* PA0..PA7 = 0 */
    gpio.Pin   = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                 GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(ST7565_DATA_PORT, &gpio);

    /* Control lines: idle CS=1 (inactive), WR=1 (idle high), A0=0 */
    ST7565_CTRL_PORT->BSRR = ST7565_CS_PIN | ST7565_WR_PIN;
    ST7565_CTRL_PORT->BSRR = ST7565_A0_PIN << 16;

    gpio.Pin   = ST7565_WR_PIN | ST7565_A0_PIN | ST7565_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(ST7565_CTRL_PORT, &gpio);

#if ST7565_USE_RESET_PIN
    HAL_GPIO_WritePin(ST7565_RES_PORT, ST7565_RES_PIN, GPIO_PIN_SET);
    gpio.Pin   = ST7565_RES_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ST7565_RES_PORT, &gpio);
#endif
}

static inline void ST7565_Strobe(uint8_t a0, uint8_t byte)
{
    /* A0 select */
    if (a0)
        ST7565_CTRL_PORT->BSRR = ST7565_A0_PIN;
    else
        ST7565_CTRL_PORT->BSRR = ST7565_A0_PIN << 16;

    /* Data on the bus before asserting CS/WR (tDS8 setup time) */
    ST7565_SetDataBus(byte);

    /* Assert chip select */
    ST7565_CTRL_PORT->BSRR = ST7565_CS_PIN << 16;
    ST7565_DelayCycles(s_strobe_delay_cycles);

    /* WR low pulse (tCCLW) -- data is latched on the rising edge */
    ST7565_CTRL_PORT->BSRR = ST7565_WR_PIN << 16;
    ST7565_DelayCycles(s_strobe_delay_cycles);
    ST7565_CTRL_PORT->BSRR = ST7565_WR_PIN;
    ST7565_DelayCycles(s_strobe_delay_cycles);

    /* Deassert chip select */
    ST7565_CTRL_PORT->BSRR = ST7565_CS_PIN;
}

void ST7565_WriteCommand(uint8_t cmd)
{
    ST7565_Strobe(0, cmd);
}

void ST7565_WriteData(uint8_t data)
{
    ST7565_Strobe(1, data);
}

void ST7565_HardReset(void)
{
#if ST7565_USE_RESET_PIN
    HAL_GPIO_WritePin(ST7565_RES_PORT, ST7565_RES_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(ST7565_RES_PORT, ST7565_RES_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
#else
    /* /RES is expected to be handled by an external RC power-on-reset
     * circuit on the board -- just give it time to release before we touch
     * the bus. */
    HAL_Delay(10);
#endif
}

/* ---------------------------------------------------------------------------
 * ST7565R command bytes, per the controller datasheet's command table.
 * -------------------------------------------------------------------------*/
#define CMD_DISPLAY_OFF          0xAE
#define CMD_DISPLAY_ON           0xAF
#define CMD_START_LINE(n)        (0x40 | ((n) & 0x3F))
#define CMD_PAGE_ADDR(n)         (0xB0 | ((n) & 0x0F))
#define CMD_COL_ADDR_HI(n)       (0x10 | (((n) >> 4) & 0x0F))
#define CMD_COL_ADDR_LO(n)       (0x00 | ((n) & 0x0F))
#define CMD_ADC_NORMAL           0xA0
#define CMD_ADC_REVERSE          0xA1
#define CMD_COMMON_NORMAL        0xC0
#define CMD_COMMON_REVERSE       0xC8
#define CMD_BIAS_1_9_OR_1_6      0xA2   /* lower bias option for this duty */
#define CMD_BIAS_1_7_OR_1_5      0xA3   /* higher bias option */
#define CMD_POWER_CTRL(b,r,f)    (0x28 | ((b)<<2) | ((r)<<1) | (f))
#define CMD_RES_RATIO(n)         (0x20 | ((n) & 0x07))
#define CMD_EV_MODE_SET          0x81   /* followed by 6-bit contrast byte */
#define CMD_RESET                0xE2

void ST7565_Init(uint8_t contrast6bit)
{
    ST7565_BusInit();
    ST7565_HardReset();

    ST7565_WriteCommand(CMD_RESET);

    ST7565_WriteCommand(CMD_ADC_NORMAL);        /* SEG mapping, flip to
                                                    CMD_ADC_REVERSE if the
                                                    image is mirrored L/R */
    ST7565_WriteCommand(CMD_COMMON_REVERSE);    /* flip to CMD_COMMON_NORMAL
                                                    if the image is upside
                                                    down */
    ST7565_WriteCommand(CMD_BIAS_1_9_OR_1_6);   /* 1/6 bias for 1/33 duty,
                                                    matches this 128x32 panel */

    /* Power up booster -> regulator -> follower in stages, per the
     * datasheet's recommended power-on sequence. */
    ST7565_WriteCommand(CMD_POWER_CTRL(1, 0, 0));
    HAL_Delay(1);
    ST7565_WriteCommand(CMD_POWER_CTRL(1, 1, 0));
    HAL_Delay(1);
    ST7565_WriteCommand(CMD_POWER_CTRL(1, 1, 1));
    HAL_Delay(1);

    ST7565_WriteCommand(CMD_RES_RATIO(7));      /* Rb/Ra ratio, tune contrast
                                                    range to taste */

    ST7565_SetContrast(contrast6bit);

    ST7565_SetStartLine(0);
    ST7565_DisplayOn(1);
}

void ST7565_DisplayOn(uint8_t on)
{
    ST7565_WriteCommand(on ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF);
}

void ST7565_SetStartLine(uint8_t line)
{
    ST7565_WriteCommand(CMD_START_LINE(line));
}

void ST7565_SetPage(uint8_t page)
{
    ST7565_WriteCommand(CMD_PAGE_ADDR(page));
}

void ST7565_SetColumn(uint8_t col)
{
    ST7565_WriteCommand(CMD_COL_ADDR_HI(col));
    ST7565_WriteCommand(CMD_COL_ADDR_LO(col));
}

void ST7565_SetContrast(uint8_t value6bit)
{
    ST7565_WriteCommand(CMD_EV_MODE_SET);
    ST7565_WriteCommand(value6bit & 0x3F);
}

void ST7565_WriteBuffer(const uint8_t *buf, uint32_t len)
{
    uint32_t pages = (len + (ST7565_WIDTH - 1)) / ST7565_WIDTH;
    if (pages > ST7565_PAGES)
        pages = ST7565_PAGES;

    for (uint32_t p = 0; p < pages; p++)
    {
        ST7565_SetPage((uint8_t)p);
        ST7565_SetColumn(0);
        for (uint32_t c = 0; c < ST7565_WIDTH; c++)
        {
            uint32_t idx = p * ST7565_WIDTH + c;
            ST7565_WriteData(idx < len ? buf[idx] : 0x00);
        }
    }
}
