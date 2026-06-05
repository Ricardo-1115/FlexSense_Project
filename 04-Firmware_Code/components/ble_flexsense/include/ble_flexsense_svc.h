#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 128-bit base: b6b6xxxx-9cf3-4a52-9f7b-6eb7b6cbf6b3 */
#define FLEX_SVC_UUID         0xFFFF
#define FLEX_CHR_DATA_UUID    0xFF01

/* BLE notification payload (8 bytes, fits in any MTU) */
typedef struct __attribute__((packed)) {
    uint16_t fsr_raw;        /* ADC raw, 0 = no press              */
    int16_t  temperature;    /* °C × 100                           */
    uint16_t humidity;       /* %RH × 10                           */
    uint16_t battery_mv;     /* mV                                 */
} flexsense_sensor_packet_t;

esp_err_t flex_svc_init(void);
void flex_svc_notify_all(void);
void flex_svc_start_notify_task(void);

#ifdef __cplusplus
}
#endif
