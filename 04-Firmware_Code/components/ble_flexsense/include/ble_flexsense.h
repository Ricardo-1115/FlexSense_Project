#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_DEVICE_NAME             "FlexSense"
#define FLEX_BLE_DATA_INTERVAL_MS      1000  /* 温湿度+电池 notify 间隔 */
#define FLEX_BLE_FSR_INTERVAL_MS        100  /* 压力 notify 间隔        */

esp_err_t ble_flexsense_init(void);

#ifdef __cplusplus
}
#endif
