#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "ble_flexsense.h"
#include "ble_flexsense_gap.h"
#include "ble_flexsense_svc.h"

static const char *TAG = "ble_flexsense";

/* ── 连接与订阅状态（svc 模块需要读取） ── */
uint16_t g_flex_conn_handle = 0;
bool     g_flex_notify_enabled = false;
bool     g_flex_notify_fsr_enabled = false;

/* ── GAP 事件 ── */
static int flex_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_flex_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected handle=%d", g_flex_conn_handle);
        } else {
            g_flex_conn_handle = 0;
            adv_init();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        g_flex_conn_handle = 0;
        g_flex_notify_enabled = false;
        g_flex_notify_fsr_enabled = false;
        adv_init();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        bool sub = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == g_flex_chr_data_handle) {
            g_flex_notify_enabled = sub;
            ESP_LOGI(TAG, "data subscribe notify=%d", sub);
        } else if (event->subscribe.attr_handle == g_flex_chr_fsr_handle) {
            g_flex_notify_fsr_enabled = sub;
            ESP_LOGI(TAG, "fsr subscribe notify=%d", sub);
        }
        return 0;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu=%d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* ── 广播 ── */
void adv_init(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params params;
    int rc;

    /* 广播数据 */
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields fail rc=%d", rc);
        return;
    }

    /* 广播参数 */
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min  = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max  = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &params, flex_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "advertising");
    } else {
        ESP_LOGE(TAG, "adv start fail rc=%d", rc);
    }
}

int gap_init(void)
{
    ble_svc_gap_init();

    int rc = ble_svc_gap_device_name_set(FLEX_DEVICE_NAME);
    if (rc != 0) return rc;

    /* 此处可设置 appearance，但不是必须 */
    return 0;
}
