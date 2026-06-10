#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "battery.h"

/* ------------------------------------------------------------------ */
/*  Hardware configuration                                             */
/* ------------------------------------------------------------------ */
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_8         /* ADC1_CH8 = GPIO9 */
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_12       /* 0 ~ 3100 mV，比 6dB 线性度更好 */
#define BATTERY_ADC_BITWIDTH    ADC_BITWIDTH_12

/* Voltage divider: R1=100k, R2=100k, ratio = 1/2
 * V_bat = V_adc * (R1+R2)/R2 = V_adc * 2 */
#define DIVIDER_RATIO           2

/* ------------------------------------------------------------------ */
/*  Filtering & timing                                                */
/* ------------------------------------------------------------------ */
#define SAMPLE_INTERVAL_MS      1000
#define FILTER_TAP_NUM          8

/* ------------------------------------------------------------------ */
/*  Module state                                                      */
/* ------------------------------------------------------------------ */
static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t        s_cali_handle = NULL;

static int32_t  s_filter_buf[FILTER_TAP_NUM];
static uint8_t  s_filter_idx = 0;
static uint32_t s_voltage_mv = 0;

/* ── 单次 ADC 采样 → 电池侧电压 (mV) ── */
static uint32_t adc_read_battery_mv(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
        return 0;
    }

    int cal_mv = 0;
    if (s_cali_handle != NULL) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &cal_mv);
    }
    if (cal_mv == 0) {
        cal_mv = (int64_t)raw * 3100 / 4095;
    }

    return (uint32_t)cal_mv * DIVIDER_RATIO;
}

/* ------------------------------------------------------------------ */
/*  ADC calibration init                                              */
/* ------------------------------------------------------------------ */
static void adc_calibration_init(adc_cali_handle_t *out_handle)
{
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = BATTERY_ADC_CHANNEL,
        .atten    = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, out_handle) == ESP_OK) {
        ESP_LOGI(TAG, "Calibration success");
    } else {
        ESP_LOGW(TAG, "No eFuse calibration, fallback to raw");
    }
}

/* ------------------------------------------------------------------ */
/*  Sample & update filtered voltage                                  */
/* ------------------------------------------------------------------ */
static void sample_battery(void)
{
    uint32_t mv = adc_read_battery_mv();
    if (mv == 0) return;

    s_filter_buf[s_filter_idx] = (int32_t)mv;
    s_filter_idx = (s_filter_idx + 1) % FILTER_TAP_NUM;

    int64_t sum = 0;
    for (int i = 0; i < FILTER_TAP_NUM; i++) sum += s_filter_buf[i];
    s_voltage_mv = (uint32_t)(sum / FILTER_TAP_NUM);
}

/* ------------------------------------------------------------------ */
/*  Background task                                                   */
/* ------------------------------------------------------------------ */
static void battery_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        sample_battery();
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void battery_init(adc_oneshot_unit_handle_t adc_handle)
{
    s_adc_handle = adc_handle;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg));

    adc_calibration_init(&s_cali_handle);

    memset(s_filter_buf, 0, sizeof(s_filter_buf));
    s_filter_idx = 0;
    s_voltage_mv = 0;

    xTaskCreatePinnedToCore(battery_task, "battery", 2048, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Battery monitoring initialized (ADC1_CH8, 12dB, 12-bit, 100k+100k)");
}

uint32_t battery_get_voltage_mv(void)
{
    return s_voltage_mv;
}

/* ── 单次 ADC 采样 ── */
uint32_t battery_read_once_mv(void)
{
    return adc_read_battery_mv();
}

/* ── 预填滤波器，确保 battery_get_voltage_mv() 立即可用 ── */
void battery_prime_filter(void)
{
    for (int i = 0; i < FILTER_TAP_NUM; i++) {
        uint32_t mv = adc_read_battery_mv();
        s_filter_buf[s_filter_idx] = (int32_t)mv;
        s_filter_idx = (s_filter_idx + 1) % FILTER_TAP_NUM;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    int64_t sum = 0;
    for (int i = 0; i < FILTER_TAP_NUM; i++) sum += s_filter_buf[i];
    s_voltage_mv = (uint32_t)(sum / FILTER_TAP_NUM);
}


