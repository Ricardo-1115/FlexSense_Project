#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 128-bit base: b6b6xxxx-9cf3-4a52-9f7b-6eb7b6cbf6b3 */
#define FLEX_SVC_UUID         0xFFFF
#define FLEX_CHR_DATA_UUID    0xFF01  /* 温湿度+电池+状态, 1s notify */
#define FLEX_CHR_FSR_UUID     0xFF02  /* FSR 压力,  100ms notify */

/* UUID128 原始字节（little-endian），广播数据和 svc 共用同一份定义 */
#define FLEX_SVC_UUID128_BYTES \
    0xb3, 0xf6, 0xcb, 0xb6, 0xb7, 0x6e, 0x7b, 0x9f, \
    0x52, 0x4a, 0xf3, 0x9c, 0xff, 0xff, 0xb6, 0xb6

/* 标志位 */
#define FLEX_FLAG_LOW_POWER   (1 << 0)  /* 低功耗模式激活 */
#define FLEX_FLAG_LOW_BATTERY (1 << 1)  /* 电池电量低，请充电 */

/* 温湿度+电池+状态数据包 (7 bytes) */
typedef struct __attribute__((packed)) {
    int16_t  temperature;    /* °C × 100                 */
    uint16_t humidity;       /* %RH × 10                 */
    uint16_t battery_mv;     /* mV                       */
    uint8_t  flags;          /* 标志位, 见 FLEX_FLAG_*   */
} flexsense_data_packet_t;

/* FSR 数据包 (2 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t fsr_raw;        /* ADC raw, 0 = no press   */
} flexsense_fsr_packet_t;

esp_err_t flex_svc_init(void);
void flex_svc_notify_all(void);
void flex_svc_notify_fsr(void);
void flex_svc_start_tasks(void);
void flex_svc_set_low_power(bool enable);
void flex_svc_set_low_battery(bool enable);

/* characteristic value handles (set after gatts_count_cfg, used by gap) */
extern uint16_t g_flex_chr_data_handle;
extern uint16_t g_flex_chr_fsr_handle;

#ifdef __cplusplus
}
#endif
