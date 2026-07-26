/*
 * fuel_level_filter.c
 *
 * Fuel level sensing on STM32G4 using ADC1, timer-triggered DMA sampling,
 * median filtering (rejects slosh spikes) + EMA (long-term smoothing)
 * + rate limiter (smooth displayed output).
 *
 * Architecture:
 *   TIM3 (10 Hz) --triggers--> ADC1 --DMA--> circular buffer
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
 *     Enable via CubeMX: ADC1 -> Parameter Settings -> Oversampling
 *     Mode. This does NOT solve slosh (that's a real mechanical
 *     signal, not electrical noise) but pairs well with the
 *     software filtering here. A ratio of 16-64x with the shift
 *     set to keep ~12-bit output range is a reasonable start.
 *   - ADC1/ADC2 share the ADC12_COMMON clock domain (ADC12CLK),
 *     sourced from AHB or a dedicated PLL output -- confirm this
 *     is enabled and running in the CubeMX clock tree.
 *   - TIM3 TRGO trigger is selected the same way as other families:
 *     ADC_EXTERNALTRIG_T3_TRGO, edge = rising.
 */

#include "main.h"     // your project's HAL includes (adjust as needed)
#include "fuel_level_filter.h"
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------- */

// ADC_SAMPLE_HZ and EMA_UPDATE_HZ are defined in fuel_level_filter.h
// so main.c can reference them too.

#define MEDIAN_WINDOW_SEC       8       // window used for median filter
#define MEDIAN_WINDOW_LEN       (ADC_SAMPLE_HZ * MEDIAN_WINDOW_SEC)

// Minimum number of real samples required before FuelSensor_Update()
// will produce output, rather than trusting a median computed from
// only a handful of samples (or a stale zero pulled before ADC1's
// first DMA conversion actually completed).
#define MEDIAN_MIN_SAMPLES      (MEDIAN_WINDOW_LEN / 2)  // half the window, ~4s worth

#define EMA_TIME_CONSTANT_SEC   3.0f   // slow smoothing after median

#define MAX_CHANGE_PERCENT_PER_SEC   5.0f  // rate limiter for displayed value

#define ADC_MAX_VALUE           4095.0f // 12-bit ADC

/* ---------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------- */

extern ADC_HandleTypeDef  hadc1;
extern TIM_HandleTypeDef  htim3;

// DMA writes raw conversions here. Single channel, circular mode,
// small buffer -- we just need the latest few conversions at a time.
#define DMA_BUF_LEN  4
static volatile uint16_t adc_dma_buf[DMA_BUF_LEN];

// Ring buffer feeding the median filter
static uint16_t median_ring[MEDIAN_WINDOW_LEN];
static uint16_t median_scratch[MEDIAN_WINDOW_LEN]; // sort workspace
static uint16_t median_ring_idx = 0;
static uint8_t  median_ring_filled = 0; // becomes 1 once buffer wraps once
static uint32_t total_samples_collected = 0; // never resets/wraps like median_ring_idx does

// Filter state
static float ema_value = -1.0f;       // -1 = "not yet initialized"
static float displayed_value = -1.0f; // final, rate-limited output (0..100%)

/* ---------------------------------------------------------------------
 * ADC / DMA setup
 *
 * Assumes CubeMX already configured:
 *   - ADC1 channel on the potentiometer's GPIO pin
 *   - ADC1 trigger source = TIM3 TRGO, external trigger conversion edge = rising
 *   - DMA1 stream for ADC1, circular mode, half-word data size
 *   - TIM3 configured for ADC_SAMPLE_HZ update rate, TRGO = update event
 * This function just starts everything.
 * ------------------------------------------------------------------- */

void FuelSensor_Init(void)
{
    memset(median_ring, 0, sizeof(median_ring));
    median_ring_idx = 0;
    median_ring_filled = 0;
    total_samples_collected = 0;
    ema_value = -1.0f;
    displayed_value = -1.0f;

    // G4 requires self-calibration before the first conversion.
    // Must be done with the ADC in a disabled state (default after
    // MX_ADC1_Init()), and only once at startup -- re-calibrating
    // repeatedly is unnecessary and will interrupt DMA sampling.
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    // Start ADC in DMA circular mode, triggered by TIM3
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, DMA_BUF_LEN);

    // Start the timer that triggers ADC conversions
    HAL_TIM_Base_Start(&htim3);
}

/* ---------------------------------------------------------------------
 * Call this periodically (e.g. every 100ms from main loop or a
 * lower-priority timer/task) to pull the latest raw ADC sample into
 * the median ring buffer. Doesn't need to be ISR-driven; DMA is
 * already free-running in the background.
 * ------------------------------------------------------------------- */

void FuelSensor_PollRawSample(void)
{
    // Grab the most recent conversion from the DMA buffer.
    // With circular DMA continuously overwriting adc_dma_buf, just
    // read the last written index; for a single-channel setup any
    // element is representative of "latest-ish" sample.
    uint16_t raw = adc_dma_buf[DMA_BUF_LEN - 1];

    median_ring[median_ring_idx] = raw;
    median_ring_idx++;
    total_samples_collected++;
    if (median_ring_idx >= MEDIAN_WINDOW_LEN) {
        median_ring_idx = 0;
        median_ring_filled = 1;
    }
}

/* ---------------------------------------------------------------------
 * @brief Returns 1 once enough real samples have been collected for
 *        FuelSensor_Update() to produce a trustworthy value, 0 while
 *        still in the startup warm-up window.
 * ------------------------------------------------------------------- */

uint8_t FuelSensor_IsReady(void)
{
    return (total_samples_collected >= MEDIAN_MIN_SAMPLES) ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * Simple insertion sort for the median scratch buffer.
 * MEDIAN_WINDOW_LEN is small (tens of elements), so O(n^2) sort is
 * fine and avoids pulling in qsort / heap allocation on an MCU.
 * ------------------------------------------------------------------- */

static void insertion_sort(uint16_t *arr, uint16_t len)
{
    for (uint16_t i = 1; i < len; i++) {
        uint16_t key = arr[i];
        int16_t j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static uint16_t compute_median(void)
{
    uint16_t len = median_ring_filled ? MEDIAN_WINDOW_LEN : median_ring_idx;
    if (len == 0) {
        return 0;
    }

    memcpy(median_scratch, median_ring, len * sizeof(uint16_t));
    insertion_sort(median_scratch, len);

    if (len % 2 == 1) {
        return median_scratch[len / 2];
    } else {
        // average the two middle values
        uint32_t a = median_scratch[len / 2 - 1];
        uint32_t b = median_scratch[len / 2];
        return (uint16_t)((a + b) / 2);
    }
}

/* ---------------------------------------------------------------------
 * Convert raw ADC counts to a fuel percentage (0-100).
 * Replace with your actual potentiometer calibration -- pot response
 * vs float position is rarely perfectly linear, so you may want a
 * lookup table instead of a straight linear map.
 * ------------------------------------------------------------------- */

static float raw_to_percent(uint16_t raw)
{
    // Example calibration points -- REPLACE with values measured on
    // your actual sender: ADC reading at empty and at full.
    //
    // NOTE: if you enable hardware oversampling in CubeMX (see header
    // comment) with a right-shift that extends resolution beyond
    // 12-bit, adc_empty/adc_full and ADC_MAX_VALUE above must be
    // re-measured/re-defined against the actual output range of the
    // oversampled value, not the raw 0-4095 12-bit range.
    const float adc_empty = 300.0f;   // raw ADC count when tank is empty
    const float adc_full  = 3800.0f;  // raw ADC count when tank is full

    float percent = (raw - adc_empty) / (adc_full - adc_empty) * 100.0f;

    if (percent < 0.0f)   percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    return percent;
}

/* ---------------------------------------------------------------------
 * Call this at EMA_UPDATE_HZ (e.g. once per second) from a scheduler,
 * RTOS task, or a free-running timer/tick counter in the main loop.
 * dt_sec should be the actual elapsed time since the last call.
 *
 * Returns the final, filtered, rate-limited fuel level in percent
 * (0.0 - 100.0), suitable for driving a gauge or display directly.
 * ------------------------------------------------------------------- */

float FuelSensor_Update(float dt_sec)
{
    // During startup, before enough real samples exist, don't let a
    // noisy/short median lock in ema_value (which initializes directly
    // from the first call). Return -1.0f as a sentinel meaning
    // "not ready yet" -- caller should hold off displaying/using the
    // value until FuelSensor_IsReady() returns 1.
    if (!FuelSensor_IsReady()) {
        return -1.0f;
    }

    uint16_t raw_median = compute_median();
    float percent_median = raw_to_percent(raw_median);

    // --- EMA stage ---
    // alpha derived from desired time constant: for a discrete EMA,
    // alpha = dt / (tau + dt)
    float alpha = dt_sec / (EMA_TIME_CONSTANT_SEC + dt_sec);

    if (ema_value < 0.0f) {
        // first run -- initialize directly to avoid a long ramp-up
        ema_value = percent_median;
    } else {
        ema_value = ema_value + alpha * (percent_median - ema_value);
    }

    // --- Rate limiter stage ---
    if (displayed_value < 0.0f) {
        displayed_value = ema_value;
    } else {
        float max_step = MAX_CHANGE_PERCENT_PER_SEC * dt_sec;
        float delta = ema_value - displayed_value;

        if (delta > max_step) {
            delta = max_step;
        } else if (delta < -max_step) {
            delta = -max_step;
        }
        displayed_value += delta;
    }

    return displayed_value;
}

/* ---------------------------------------------------------------------
 * Example usage (pseudo main loop):
 *
 * FuelSensor_Init();
 *
 * uint32_t last_poll_tick = HAL_GetTick();
 * uint32_t last_update_tick = HAL_GetTick();
 *
 * while (1) {
 *     uint32_t now = HAL_GetTick();
 *
 *     // Poll raw ADC sample at ADC_SAMPLE_HZ
 *     if (now - last_poll_tick >= (1000 / ADC_SAMPLE_HZ)) {
 *         last_poll_tick = now;
 *         FuelSensor_PollRawSample();
 *     }
 *
 *     // Run EMA + rate limiter at EMA_UPDATE_HZ
 *     if (now - last_update_tick >= (uint32_t)(1000.0f / EMA_UPDATE_HZ)) {
 *         float dt = (now - last_update_tick) / 1000.0f;
 *         last_update_tick = now;
 *         float fuel_pct = FuelSensor_Update(dt);
 *         // -> send fuel_pct to display/gauge/CAN message/etc.
 *     }
 *
 *     // ... other tasks ...
 * }
 * ------------------------------------------------------------------- */
