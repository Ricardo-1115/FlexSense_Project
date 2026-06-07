#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 128-bit base: b6b6xxxx-9cf3-4a52-9f7b-6eb7b6cbf6b3 */
#define FLEX_SVC_UUID         0xFFFF
#define FLEX_CHR_DATA_UUID    0xFF01  /* 温湿度+电池, 1s notify */
#define FLEX_CHR_FSR_UUID     0xFF02  /* FSR 压力,  100ms notify */

/* 温湿度+电池数据包 (6 bytes) */
typedef struct __attribute__((packed)) {
    int16_t  temperature;    /* °C × 100                 */
    uint16_t humidity;       /* %RH × 10                 */
    uint16_t battery_mv;     /* mV                       */
} flexsense_data_packet_t;

/* FSR 数据包 (2 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t fsr_raw;        /* ADC raw, 0 = no press   */
} flexsense_fsr_packet_t;

esp_err_t flex_svc_init(void);
void flex_svc_notify_all(void);
void flex_svc_notify_fsr(void);
void flex_svc_start_tasks(void);

/* characteristic value handles (set after gatts_count_cfg, used by gap) */
extern uint16_t g_flex_chr_data_handle;
extern uint16_t g_flex_chr_fsr_handle;

#ifdef __cplusplus
}
#endif
