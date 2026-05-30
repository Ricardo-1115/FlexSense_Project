#pragma once

#include "driver/i2c_master.h"
#include "fsr402.h"
#include "sht31.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 共享 I2C 主总线句柄 — main.c 初始化，各模块共用 */
extern i2c_master_bus_handle_t flexsense_i2c_bus;

/** 共享 FSR402 句柄 — main.c 初始化，sensor task 和 debug 命令共用 */
extern fsr402_handle_ptr_t     flexsense_fsr;

/** 共享 SHT31 句柄 — main.c 初始化，sensor task 和 debug 命令共用 */
extern sht31_handle_ptr_t      flexsense_sht31;

void register_flexsense(void);

#ifdef __cplusplus
}
#endif
