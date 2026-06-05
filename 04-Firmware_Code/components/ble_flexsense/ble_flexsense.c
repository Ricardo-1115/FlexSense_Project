#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_flexsense.h"
#include "ble_flexsense_gap.h"
#include "ble_flexsense_svc.h"

static const char *TAG = "ble_flexsense";

/* ── 协议栈事件 ── */
static void flex_on_reset(int reason)
{
    ESP_LOGW(TAG, "reset reason=%d", reason);
}

static void flex_on_sync(void)
{
    ESP_LOGI(TAG, "sync ok");
    adv_init();
}

/* ── 主机任务 ── */
static void flex_host_task(void *param)
{
    ESP_LOGI(TAG, "host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ── 公开接口 ── */
esp_err_t ble_flexsense_init(void)
{
    int rc;

    /* NVS 必须提前初始化好（main.c 已做）*/

    rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = flex_on_reset;
    ble_hs_cfg.sync_cb  = flex_on_sync;
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "gap_init failed: %d", rc);
        return ESP_FAIL;
    }

    rc = flex_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "flex_svc_init failed: %d", rc);
        return ESP_FAIL;
    }
    flex_svc_start_notify_task();

    nimble_port_freertos_init(flex_host_task);

    ESP_LOGI(TAG, "BLE initialized, device name: " FLEX_DEVICE_NAME);
    return ESP_OK;
}
