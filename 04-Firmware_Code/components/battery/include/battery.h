#pragma once

#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

void battery_init(adc_oneshot_unit_handle_t adc_handle);

/**
 * @brief 用实时采样预填滤波器环形缓冲区
 *        在电源稳定后调用一次，此后 battery_get_voltage_mv() 立即返回有效值。
 */
void battery_prime_filter(void);

uint32_t battery_get_voltage_mv(void);

/**
 * @brief 单次 ADC 采样，返回电池侧电压 (mV)
 */
uint32_t battery_read_once_mv(void);

#ifdef __cplusplus
}
#endif
