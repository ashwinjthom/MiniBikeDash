// Author: Ashwin Thomas

#include "Rev_Counter.h"

static TIM_HandleTypeDef *capture_timer;
static uint32_t capture_channel;
static volatile uint16_t previous_capture;
static volatile uint32_t engine_rpm;
static volatile uint32_t last_capture_ms;
static volatile uint8_t have_previous_capture;
static uint8_t ignore_this_capture;

static void RevCounter_ProcessCapture(TIM_HandleTypeDef *htim,
                                      uint32_t pulses_in_period)
{
    uint16_t capture;
    uint16_t ticks;
    const uint32_t minimum_ticks_per_pulse =
        (REV_COUNTER_TIMER_HZ * 60UL) /
        (REV_COUNTER_MAX_RPM * REV_COUNTER_PULSES_PER_REV);

    if ((htim != capture_timer) ||
        (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_2)) {
        return;
    }

    capture = (uint16_t)HAL_TIM_ReadCapturedValue(htim, capture_channel);
    last_capture_ms = HAL_GetTick();

    if (have_previous_capture != 0U) {
        /* Unsigned subtraction handles one 16-bit TIM3 counter wrap. */
        ticks = (uint16_t)(capture - previous_capture);

        /* Reject ignition noise faster than the configured maximum RPM. */
        if ((uint32_t)ticks >= (minimum_ticks_per_pulse * pulses_in_period)) {
            engine_rpm = (REV_COUNTER_TIMER_HZ * 60UL) /
                         ((uint32_t)ticks * pulses_in_period);
        }
    }

    previous_capture = capture;
    have_previous_capture = 1U;
}

HAL_StatusTypeDef RevCounter_Start(TIM_HandleTypeDef *htim, uint32_t channel)
{
    if ((htim == NULL) || (channel != TIM_CHANNEL_2)) {
        return HAL_ERROR;
    }

    capture_timer = htim;
    capture_channel = channel;
    previous_capture = 0U;
    engine_rpm = 0U;
    last_capture_ms = HAL_GetTick();
    have_previous_capture = 0U;
    ignore_this_capture = 1U;

    return HAL_TIM_IC_Start_IT(capture_timer, capture_channel);
}

void RevCounter_OnCapture(TIM_HandleTypeDef *htim)
{
    RevCounter_ProcessCapture(htim, REV_COUNTER_PULSES_PER_REV);
}

void RevCounter_OnEveryOtherCapture(TIM_HandleTypeDef *htim)
{
    if ((htim != capture_timer) ||
        (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_2)) {
        return;
    }

    ignore_this_capture ^= 1U;
    if (ignore_this_capture != 0U) {
        return;
    }

    RevCounter_ProcessCapture(htim, REV_COUNTER_PULSES_PER_REV);
}

void RevCounter_Update(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - last_capture_ms) >= REV_COUNTER_STOP_TIMEOUT_MS) {
        engine_rpm = 0U;
        have_previous_capture = 0U;
    }
}

uint32_t RevCounter_GetRPM(void)
{
    return engine_rpm;
}

