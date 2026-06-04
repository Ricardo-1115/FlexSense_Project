#pragma once

#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

void battery_init(adc_oneshot_unit_handle_t adc_handle);

uint32_t battery_get_voltage_mv(void);

#ifdef __cplusplus
}
#endif
