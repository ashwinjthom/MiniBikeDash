/* Filtered battery-capacity sampling on ADC2_IN15 (PB15). */
#ifndef BATTERY_LEVEL_FILTER_H
#define BATTERY_LEVEL_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Measured voltage at PB15, not the battery-side voltage of the divider. */
#define BATTERY_ADC_REFERENCE_VOLTS           3.3f
#define BATTERY_ADC_EMPTY_VOLTS               3.00f
#define BATTERY_ADC_FULL_VOLTS                3.18f

void BatterySensor_Init(void);
void BatterySensor_PollRawSample(void);
uint8_t BatterySensor_IsReady(void);
/* Returns battery capacity in percent (0-100), or -1.0f while warming up. */
float BatterySensor_Update(float dt_sec);
uint16_t BatterySensor_GetMedianRaw(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_LEVEL_FILTER_H */
