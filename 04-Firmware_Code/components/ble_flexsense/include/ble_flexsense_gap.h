#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 连接与通知状态（svc 模块引用） */
extern uint16_t g_flex_conn_handle;
extern bool     g_flex_notify_enabled;

void adv_init(void);
int gap_init(void);

#ifdef __cplusplus
}
#endif
