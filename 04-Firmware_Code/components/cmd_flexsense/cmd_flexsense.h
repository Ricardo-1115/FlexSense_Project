#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Shared I2C master bus handle, initialized in main.c */
extern i2c_master_bus_handle_t flexsense_i2c_bus;

void register_flexsense(void);

#ifdef __cplusplus
}
#endif
