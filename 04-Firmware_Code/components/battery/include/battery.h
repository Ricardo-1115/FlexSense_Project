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
 * @brief 重新采样取平均，返回当前电池电压 (mV)
 *        用于刚上电时避开浪涌电流导致的偏低读数。
 *        内部做多次 ADC 采样（带小间隔），结果不参与后台滤波。
 */
uint32_t battery_read_fresh_mv(void);

#ifdef __cplusplus
}
#endif
