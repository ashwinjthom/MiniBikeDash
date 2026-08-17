/*
 * fuel_level_filter.h
 *
 *  Created on: Jul 27, 2026
 * 		Author: Ashwin Thomas
 *
 * Public API for fuel level sensing on STM32G4 (ADC1 + TIM6 + DMA),
 * with median filtering, EMA smoothing, and rate-limited output.
 *
 * See fuel_level_filter.c for full implementation notes.
 */

#ifndef FUEL_LEVEL_FILTER_H
#define FUEL_LEVEL_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------- */

/**
 * @brief Initialize the fuel sensor filter chain and start ADC1/TIM6.
 *
 * Performs ADC self-calibration (required on G4), starts ADC1 in
 * circular DMA mode, and starts TIM3 to trigger conversions at
 * ADC_SAMPLE_HZ. Call once at startup, after MX_ADC1_Init() and
 * MX_TIM3_Init() (or their CubeMX-generated equivalents) have run.
 */
void FuelSensor_Init(void);

/**
 * @brief Pull the latest raw ADC sample into the median filter's
 *        ring buffer.
 *
 * Call periodically, e.g. every 100 ms (matching ADC_SAMPLE_HZ) from
 * the main loop or a low-priority task/timer. Does not need to be
 * ISR-driven -- DMA sampling runs independently in the background.
 */
void FuelSensor_PollRawSample(void);

/**
 * @brief Run the median -> EMA -> rate-limiter pipeline and return
 *        the current filtered fuel level.
 *
 * Call periodically, e.g. once per second (matching EMA_UPDATE_HZ)
 * from the main loop, a scheduler, or an RTOS task.
 *
 * @param dt_sec  Actual elapsed time in seconds since the previous
 *                call. Used to compute the EMA coefficient and the
 *                rate limiter's max step, so pass the real measured
 *                interval rather than assuming a fixed period.
 *
 * @return Filtered, rate-limited fuel level as a percentage
 *         (0.0 - 100.0), suitable for driving a gauge, display, or
 *         CAN message directly.
 */
float FuelSensor_Update(float dt_sec);

/**
 * @brief Check whether enough real samples have been collected since
 *        FuelSensor_Init() for FuelSensor_Update() to return a
 *        trustworthy value.
 *
 * While this returns 0, FuelSensor_Update() returns -1.0f (a sentinel,
 * not a valid percentage) instead of a real reading. Check this (or
 * just check for a negative return from FuelSensor_Update()) before
 * displaying/using the fuel level during the first several seconds
 * after startup.
 *
 * @return 1 if ready, 0 if still warming up.
 */
uint8_t FuelSensor_IsReady(void);

/* ---------------------------------------------------------------------
 * Config constants -- exposed here (not just in the .c file) so
 * main.c can reference them directly, e.g. for computing polling
 * intervals with HAL_GetTick(), instead of hardcoding matching
 * numbers in two separate files.
 * ------------------------------------------------------------------- */

#define ADC_SAMPLE_HZ           10      // how often TIM6 triggers ADC1 / how often to call FuelSensor_PollRawSample()
#define EMA_UPDATE_HZ           1.0f    // how often to call FuelSensor_Update()

#ifdef __cplusplus
}
#endif

#endif /* FUEL_LEVEL_FILTER_H */
