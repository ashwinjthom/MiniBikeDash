#ifndef INC_REV_COUNTER_H_
#define INC_REV_COUNTER_H_

#include "main.h"

/* TIM3 is configured for a 100 kHz counter clock: one tick = 10 us. */
#define REV_COUNTER_TIMER_HZ          100000UL
#define REV_COUNTER_PULSES_PER_REV    1UL
#define REV_COUNTER_MAX_RPM           10000UL
#define REV_COUNTER_STOP_TIMEOUT_MS   750UL

/* Starts capture on the configured timer channel. */
HAL_StatusTypeDef RevCounter_Start(TIM_HandleTypeDef *htim, uint32_t channel);

/* Call this from HAL_TIM_IC_CaptureCallback(). */
void RevCounter_OnCapture(TIM_HandleTypeDef *htim);

/*
 * Call this instead of RevCounter_OnCapture() to use every second falling
 * edge. RPM remains correct because each measured period spans two revolutions.
 */
void RevCounter_OnEveryOtherCapture(TIM_HandleTypeDef *htim);

/* Call regularly from the main loop to report zero after the engine stops. */
void RevCounter_Update(uint32_t now_ms);

/* Returns the most recently calculated whole-engine RPM. */
uint32_t RevCounter_GetRPM(void);



#endif /* INC_REV_COUNTER_H_ */
