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
 * b6b6ffff-9cf3-4a52-9f7b-6eb7b6cbf6b3
 */
static const ble_uuid128_t flex_svc_uuid =
    BLE_UUID128_INIT(0xb3, 0xf6, 0xcb, 0xb6, 0xb7, 0x6e, 0x7b, 0x9f,
                     0x52, 0x4a, 0xf3, 0x9c, 0xff, 0xff, 0xb6, 0xb6);

static const ble_uuid128_t flex_chr_data_uuid =
    BLE_UUID128_INIT(0xb3, 0xf6, 0xcb, 0xb6, 0xb7, 0x6e, 0x7b, 0x9f,
                     0x52, 0x4a, 0xf3, 0x9c, 0xff, 0x01, 0xb6, 0xb6);

static uint16_t flex_chr_data_handle;

/* ── GATT 访问回调 ── */
static int flex_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        flexsense_sensor_packet_t pkt = { 0 };

        /* 读操作时返回当前传感器值 */
        if (flexsense_fsr) {
            fsr402_data_t d;
            if (fsr402_read(flexsense_fsr, &d) == ESP_OK) {
                pkt.fsr_raw = d.adc_raw;
            }
        }
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

/* ── 服务定义 ── */
static const struct ble_gatt_svc_def flex_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &flex_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid       = &flex_chr_data_uuid.u,
            .access_cb  = flex_chr_access,
            .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &flex_chr_data_handle,
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

    ESP_LOGI(TAG, "svc registered");
    return ESP_OK;
}

/* ── 发送通知 ── */
void flex_svc_notify_all(void)
{
    if (g_flex_conn_handle == 0 || !g_flex_notify_enabled) return;
    if (flex_chr_data_handle == 0) return;

    flexsense_sensor_packet_t pkt = { 0 };

    /* FSR402 */
    if (flexsense_fsr) {
        fsr402_data_t d;
        if (fsr402_read(flexsense_fsr, &d) == ESP_OK) {
            pkt.fsr_raw = d.adc_raw;
        }
    }

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

    int rc = ble_gatts_notify_custom(g_flex_conn_handle, flex_chr_data_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify fail rc=%d", rc);
    }
}

/* ── 后台通知任务 ── */
static void flex_notify_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FLEX_BLE_NOTIFY_INTERVAL_MS));
        flex_svc_notify_all();
    }
}

/* 在 flex_svc_init 最后启动任务 */
/* 用 C11 _Constructor 或显式调用来启动都可以，
   这里改为在 ble_flexsense_init 中调用 flex_svc_start_notify_task() */
void flex_svc_start_notify_task(void)
{
    static TaskHandle_t task = NULL;
    if (task) return;
    xTaskCreatePinnedToCore(flex_notify_task, "flex_notify", 3072,
                            NULL, 5, &task, 1);
}
