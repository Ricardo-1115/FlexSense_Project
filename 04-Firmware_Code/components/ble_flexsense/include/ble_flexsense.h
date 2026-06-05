#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_DEVICE_NAME           "FlexSense"
#define FLEX_BLE_NOTIFY_INTERVAL_MS   1000

esp_err_t ble_flexsense_init(void);

#ifdef __cplusplus
}
#endif
