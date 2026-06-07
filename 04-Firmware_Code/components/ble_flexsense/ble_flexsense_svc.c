#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_flexsense.h"
#include "ble_flexsense_gap.h"
#include "ble_flexsense_svc.h"

/* ── 传感器接口 ── */
#include "cmd_flexsense.h"
#include "battery.h"

static const char *TAG = "ble_flexsense";

/* ── 128-bit UUID (little-endian) ──
 * b6b6xxxx-9cf3-4a52-9f7b-6eb7b6cbf6b3
 */
static const ble_uuid128_t flex_svc_uuid =
    BLE_UUID128_INIT(0xb3, 0xf6, 0xcb, 0xb6, 0xb7, 0x6e, 0x7b, 0x9f,
                     0x52, 0x4a, 0xf3, 0x9c, 0xff, 0xff, 0xb6, 0xb6);

/* 0xFF01: 温湿度+电池 */
static const ble_uuid128_t flex_chr_data_uuid =
    BLE_UUID128_INIT(0xb3, 0xf6, 0xcb, 0xb6, 0xb7, 0x6e, 0x7b, 0x9f,
                     0x52, 0x4a, 0xf3, 0x9c, 0x01, 0xff, 0xb6, 0xb6);

/* 0xFF02: FSR 压力 */
static const ble_uuid128_t flex_chr_fsr_uuid =
    BLE_UUID128_INIT(0xb3, 0xf6, 0xcb, 0xb6, 0xb7, 0x6e, 0x7b, 0x9f,
                     0x52, 0x4a, 0xf3, 0x9c, 0x02, 0xff, 0xb6, 0xb6);

/* characteristic value handles (exported for gap subscribe event) */
uint16_t g_flex_chr_data_handle;
uint16_t g_flex_chr_fsr_handle;

/* ── GATT 访问回调: 0xFF01 (温湿度+电池) ── */
static int flex_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        flexsense_data_packet_t pkt = { 0 };

        if (flexsense_sht31) {
            sht31_data_t d;
            if (sht31_measure(flexsense_sht31, SHT31_REPEAT_HIGH, &d) == ESP_OK) {
                pkt.temperature = (int16_t)(d.temperature * 100);
                pkt.humidity    = (uint16_t)(d.humidity * 10);
            }
        }
        pkt.battery_mv = (uint16_t)battery_get_voltage_mv();

        int rc = os_mbuf_append(ctxt->om, &pkt, sizeof(pkt));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

/* ── GATT 访问回调: 0xFF02 (FSR 压力) ── */
static int flex_chr_fsr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint16_t fsr_raw = 0;
        if (flexsense_fsr) {
            fsr402_data_t d;
            if (fsr402_read(flexsense_fsr, &d) == ESP_OK) {
                fsr_raw = d.adc_raw;
            }
        }
        int rc = os_mbuf_append(ctxt->om, &fsr_raw, sizeof(fsr_raw));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

/* ── 服务定义 ── */
static const struct ble_gatt_svc_def flex_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &flex_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid       = &flex_chr_data_uuid.u,
            .access_cb  = flex_chr_access,
            .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &g_flex_chr_data_handle,
        }, {
            .uuid       = &flex_chr_fsr_uuid.u,
            .access_cb  = flex_chr_fsr_access,
            .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &g_flex_chr_fsr_handle,
        }, {
            0, /* terminator */
        } },
    },
    { 0 }, /* terminator */
};

/* ── 注册服务 ── */
esp_err_t flex_svc_init(void)
{
    int rc;

    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(flex_svcs);
    if (rc != 0) return ESP_FAIL;

    rc = ble_gatts_add_svcs(flex_svcs);
    if (rc != 0) return ESP_FAIL;

    ESP_LOGI(TAG, "svc registered (handle data=0x%x fsr=0x%x)",
             g_flex_chr_data_handle, g_flex_chr_fsr_handle);
    return ESP_OK;
}

/* ── 发送通知: 0xFF01 温湿度+电池 (1s) ── */
void flex_svc_notify_all(void)
{
    if (g_flex_conn_handle == 0 || !g_flex_notify_enabled) return;
    if (g_flex_chr_data_handle == 0) return;

    flexsense_data_packet_t pkt = { 0 };

    /* SHT31 */
    if (flexsense_sht31) {
        sht31_data_t d;
        if (sht31_measure(flexsense_sht31, SHT31_REPEAT_HIGH, &d) == ESP_OK) {
            pkt.temperature = (int16_t)(d.temperature * 100);
            pkt.humidity    = (uint16_t)(d.humidity * 10);
        }
    }

    /* 电池 */
    pkt.battery_mv = (uint16_t)battery_get_voltage_mv();

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&pkt, sizeof(pkt));
    if (!om) return;

    int rc = ble_gatts_notify_custom(g_flex_conn_handle, g_flex_chr_data_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "data notify fail rc=%d", rc);
    }
}

/* ── 发送通知: 0xFF02 FSR 压力 (100ms) ── */
void flex_svc_notify_fsr(void)
{
    if (g_flex_conn_handle == 0 || !g_flex_notify_fsr_enabled) return;
    if (g_flex_chr_fsr_handle == 0) return;

    uint16_t fsr_raw = 0;
    if (flexsense_fsr) {
        fsr402_data_t d;
        if (fsr402_read(flexsense_fsr, &d) == ESP_OK) {
            fsr_raw = d.adc_raw;
        }
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&fsr_raw, sizeof(fsr_raw));
    if (!om) return;

    int rc = ble_gatts_notify_custom(g_flex_conn_handle, g_flex_chr_fsr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "fsr notify fail rc=%d", rc);
    }
}

/* ── 后台任务 ── */
static void flex_data_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FLEX_BLE_DATA_INTERVAL_MS));
        flex_svc_notify_all();
    }
}

static void flex_fsr_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FLEX_BLE_FSR_INTERVAL_MS));
        flex_svc_notify_fsr();
    }
}

void flex_svc_start_tasks(void)
{
    static bool started = false;
    if (started) return;
    started = true;

    xTaskCreatePinnedToCore(flex_data_task, "flex_data", 3072,
                            NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(flex_fsr_task, "flex_fsr", 3072,
                            NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "notify tasks started (data=%dms fsr=%dms)",
             FLEX_BLE_DATA_INTERVAL_MS, FLEX_BLE_FSR_INTERVAL_MS);
}
