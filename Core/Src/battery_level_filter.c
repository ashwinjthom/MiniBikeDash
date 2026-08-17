// Author: Ashwin Thomas

/*
 * battery_level_filter.c
 *
 * Battery level sensing on STM32G4 using ADC2, timer-triggered DMA sampling,
 * median filtering (rejects voltage spikes)
 *
 * Architecture:
 *   TIM6 (10 Hz) --triggers--> ADC1 --DMA--> circular buffer
 *   Main loop / low-priority task reads new samples, pushes into a
 *   ring buffer, computes median over ~6-12s window, feeds median
 *   into a slow EMA, then rate-limits the final displayed value.
 *
 * Tune the #defines below to your tank size, ADC vref, and desired
 * response time.
 *
 * STM32G4-specific notes:
 *   - ADC1 requires a self-calibration (HAL_ADCEx_Calibration_Start)
 *     before the first conversion. This is handled in FuelSensor_Init().
 *   - G4's ADC supports hardware oversampling (up to 1024x with
 *     configurable right-shift), which reduces raw sample jitter
 *     for free before it ever reaches the median filter below.
 *     Enable via CubeMX: ADC2 -> Parameter Settings -> Oversampling
 *     Mode. A ratio of 16-64x with the shift
 *     set to keep ~12-bit output range is a reasonable start.
 *   - ADC1/ADC2 share the ADC12_COMMON clock domain (ADC12CLK),
 *     sourced from AHB or a dedicated PLL output -- confirm this
 *     is enabled and running in the CubeMX clock tree.
 *   - TIM6 TRGO trigger is selected the same way as other families:
 *     ADC_EXTERNALTRIG_T3_TRGO, edge = rising.
 */


/* ADC2_IN15 / PB15 battery-capacity filter: median -> EMA -> rate limiter. */
#include "main.h"
#include "battery_level_filter.h"
#include "fuel_level_filter.h"
#include <string.h>

#define MEDIAN_WINDOW_SEC                 8U
#define MEDIAN_WINDOW_LEN                 (ADC_SAMPLE_HZ * MEDIAN_WINDOW_SEC)
#define MEDIAN_MIN_SAMPLES                (MEDIAN_WINDOW_LEN / 2U)
#define DMA_BUF_LEN                       4U
#define EMA_TIME_CONSTANT_SEC             3.0f
#define MAX_CHANGE_PERCENT_PER_SEC         5.0f
#define ADC_MAX_VALUE                     4095.0f

extern ADC_HandleTypeDef hadc2;
static volatile uint16_t adc_dma_buf[DMA_BUF_LEN];
static uint16_t median_ring[MEDIAN_WINDOW_LEN];
static uint16_t median_scratch[MEDIAN_WINDOW_LEN];
static uint16_t median_ring_idx;
static uint16_t median_raw;
static uint32_t total_samples_collected;
static float ema_percent = -1.0f;
static float displayed_percent = -1.0f;

static void insertion_sort(uint16_t *values, uint16_t len)
{
    for (uint16_t i = 1U; i < len; i++) {
        uint16_t key = values[i];
        int16_t j = (int16_t)i - 1;
        while ((j >= 0) && (values[j] > key)) { values[j + 1] = values[j]; j--; }
        values[j + 1] = key;
    }
}

static uint16_t compute_median(void)
{
    uint16_t len = (total_samples_collected < MEDIAN_WINDOW_LEN) ?
                   (uint16_t)total_samples_collected : MEDIAN_WINDOW_LEN;
    if (len == 0U) return 0U;
    memcpy(median_scratch, median_ring, len * sizeof(median_scratch[0]));
    insertion_sort(median_scratch, len);
    if ((len & 1U) != 0U) return median_scratch[len / 2U];
    return (uint16_t)(((uint32_t)median_scratch[(len / 2U) - 1U] + median_scratch[len / 2U]) / 2U);
}

static float raw_to_battery_percent(uint16_t raw)
{
    float pin_voltage = ((float)raw * BATTERY_ADC_REFERENCE_VOLTS) / ADC_MAX_VALUE;
    float percent = ((pin_voltage - BATTERY_ADC_EMPTY_VOLTS) /
                     (BATTERY_ADC_FULL_VOLTS - BATTERY_ADC_EMPTY_VOLTS)) * 100.0f;
    if (percent < 0.0f) return 0.0f;
    if (percent > 100.0f) return 100.0f;
    return percent;
}

void BatterySensor_Init(void)
{
    memset(median_ring, 0, sizeof(median_ring));
    median_ring_idx = 0U; median_raw = 0U; total_samples_collected = 0U;
    ema_percent = -1.0f; displayed_percent = -1.0f;
    if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc_dma_buf, DMA_BUF_LEN) != HAL_OK) Error_Handler();
}

void BatterySensor_PollRawSample(void)
{
    median_ring[median_ring_idx] = adc_dma_buf[DMA_BUF_LEN - 1U];
    median_ring_idx = (median_ring_idx + 1U) % MEDIAN_WINDOW_LEN;
    total_samples_collected++;
}

uint8_t BatterySensor_IsReady(void)
{
    return (total_samples_collected >= MEDIAN_MIN_SAMPLES) ? 1U : 0U;
}

float BatterySensor_Update(float dt_sec)
{
    float median_percent, alpha;
    if (!BatterySensor_IsReady() || (dt_sec <= 0.0f)) return -1.0f;
    median_raw = compute_median();
    median_percent = raw_to_battery_percent(median_raw);
    alpha = dt_sec / (EMA_TIME_CONSTANT_SEC + dt_sec);
    if (ema_percent < 0.0f) ema_percent = median_percent;
    else ema_percent += alpha * (median_percent - ema_percent);
    if (displayed_percent < 0.0f) displayed_percent = ema_percent;
    else {
        float max_step = MAX_CHANGE_PERCENT_PER_SEC * dt_sec;
        float delta = ema_percent - displayed_percent;
        if (delta > max_step) delta = max_step;
        else if (delta < -max_step) delta = -max_step;
        displayed_percent += delta;
    }
    return displayed_percent;
}

uint16_t BatterySensor_GetMedianRaw(void) { return median_raw; }
