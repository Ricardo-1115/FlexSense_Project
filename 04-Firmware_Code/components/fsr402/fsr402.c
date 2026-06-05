/*
 * @file fsr402.c
 * @brief FSR402 压敏电阻 ADC 驱动
 *
 * 数据链路: FSR402 → 分压电路 → ADC 采样 → 电压校准 → 算电阻 → 算压力
 * 分压公式: R_fsr = R_fixed × (Vcc − Vout) / Vout
 * 压力估算: F = force_k / R_fsr（电导与压力在 log-log 下近似成正比）
 */

#include <string.h>
#include "fsr402.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "fsr402";

/* ===================================================================
 *  内部结构体
 * =================================================================== */

struct fsr402_handle_t {
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t         cali_handle;
    fsr402_config_t           config;
    bool                      calibrated;
};

static struct fsr402_handle_t s_fsr = {0};

/* ===================================================================
 *  ADC 校准初始化
 * ===================================================================
 */

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                 adc_atten_t atten,
                                 adc_cali_handle_t *out_handle)
{
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, out_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC 校准成功 (Curve Fitting)");
        return true;
    }
    ESP_LOGW(TAG, "eFuse 未烧录，将使用线性估算电压");
    return false;
}

/* ===================================================================
 *  预定义配置
 * =================================================================== */

const fsr402_config_t fsr402_breadboard_cfg = {
    .adc_channel  = ADC_CHANNEL_9,
    .adc_bitwidth = ADC_BITWIDTH_12,
    .adc_atten    = ADC_ATTEN_DB_12,
    .v_ref_mv     = 3300,
    .r_fixed      = 10000,
    .force_k      = 5000.0f,
};

const fsr402_config_t fsr402_pcb_cfg = {
    .adc_channel  = ADC_CHANNEL_9,
    .adc_bitwidth = ADC_BITWIDTH_12,
    .adc_atten    = ADC_ATTEN_DB_12,
    .v_ref_mv     = 3300,
    .r_fixed      = 30000,
    .force_k      = 5000.0f,
};

/* ===================================================================
 *  初始化 & 销毁
 * =================================================================== */

esp_err_t fsr402_init(const fsr402_config_t *cfg, fsr402_handle_ptr_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "参数不能为空");
    ESP_RETURN_ON_FALSE(!s_fsr.adc_handle, ESP_ERR_INVALID_STATE, TAG, "重复初始化");

    esp_err_t ret;
    memset(&s_fsr, 0, sizeof(s_fsr));
    memcpy(&s_fsr.config, cfg, sizeof(*cfg));

    /* ADC oneshot 单元 */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ret = adc_oneshot_new_unit(&unit_cfg, &s_fsr.adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 初始化失败 (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* ADC 通道 */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = cfg->adc_atten,
        .bitwidth = cfg->adc_bitwidth,
    };
    ret = adc_oneshot_config_channel(s_fsr.adc_handle, cfg->adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败 (%s)", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_fsr.adc_handle);
        s_fsr.adc_handle = NULL;
        return ret;
    }

    /* 电压校准（失败不影响读取） */
    s_fsr.calibrated = adc_calibration_init(ADC_UNIT_1, cfg->adc_channel,
                                            cfg->adc_atten,
                                            &s_fsr.cali_handle);

    *out_handle = &s_fsr;
    return ESP_OK;
}

esp_err_t fsr402_deinit(fsr402_handle_ptr_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "句柄为空");

    if (handle->calibrated && handle->cali_handle) {
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle->cali_handle));
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(handle->adc_handle));
    memset(handle, 0, sizeof(*handle));
    return ESP_OK;
}

/* ===================================================================
 *  数据转换
 * =================================================================== */

/* 衰减 → ADC 满量程电压，无校准时线性估算用（ESP32-S3 Vref ≈ 1100 mV） */
static int atten_fullscale_mv(adc_atten_t atten)
{
    switch (atten) {
        case ADC_ATTEN_DB_0:   return 1100;
        case ADC_ATTEN_DB_2_5: return 1474;
        case ADC_ATTEN_DB_6:   return 2200;
        default:               return 3100;   /* DB_12 */
    }
}

/* ADC 原始值 → 电压 (mV)，优先 eFuse 校准 */
static int raw_to_voltage(fsr402_handle_ptr_t h, int raw)
{
    if (h->calibrated) {
        int voltage;
        if (adc_cali_raw_to_voltage(h->cali_handle, raw, &voltage) == ESP_OK) {
            return voltage;
        }
    }
    int max_raw = (1 << h->config.adc_bitwidth) - 1;
    return (int)((uint64_t)raw * atten_fullscale_mv(h->config.adc_atten) / max_raw);
}

/* Vout → R_fsr，分压公式反推 */
static uint32_t voltage_to_resistance(const fsr402_config_t *cfg, int voltage_mv)
{
    if (voltage_mv <= 0) return UINT32_MAX;           /* 开路 */
    if (voltage_mv >= (int)cfg->v_ref_mv) return 0;   /* 短路 */
    return (uint32_t)((uint64_t)cfg->r_fixed * (cfg->v_ref_mv - voltage_mv) / voltage_mv);
}

/* R_fsr → 力：F = force_k / R */
static float resistance_to_force(const fsr402_config_t *cfg, uint32_t resistance)
{
    if (resistance == 0 || resistance >= 1000000) return 0.0f;
    return cfg->force_k / (float)resistance;
}

/* 原始 ADC 值 → 填充输出结构体（读接口共用） */
static void data_from_raw(fsr402_handle_ptr_t h, int raw, fsr402_data_t *out)
{
    int voltage = raw_to_voltage(h, raw);
    uint32_t resistance = voltage_to_resistance(&h->config, voltage);
    out->adc_raw       = raw;
    out->voltage_mv    = voltage;
    out->resistance    = resistance;
    out->force_n       = resistance_to_force(&h->config, resistance);
    out->is_pressed    = (resistance < 1000000);
    out->timestamp_us  = esp_timer_get_time();
    out->calibrated    = h->calibrated;
    out->voltage_valid = (raw > 0);
}

/* ===================================================================
 *  ADC 句柄访问
 * =================================================================== */

adc_oneshot_unit_handle_t fsr402_get_adc_handle(fsr402_handle_ptr_t handle)
{
    return handle ? handle->adc_handle : NULL;
}

/* ===================================================================
 *  对外读取接口
 * =================================================================== */

esp_err_t fsr402_read(fsr402_handle_ptr_t handle, fsr402_data_t *out)
{
    ESP_RETURN_ON_FALSE(handle && out, ESP_ERR_INVALID_ARG, TAG, "参数为空");
    int raw;
    esp_err_t ret = adc_oneshot_read(handle->adc_handle, handle->config.adc_channel, &raw);
    if (ret != ESP_OK) return ret;
    data_from_raw(handle, raw, out);
    return ESP_OK;
}

esp_err_t fsr402_read_avg(fsr402_handle_ptr_t handle, int samples,
                          fsr402_data_t *out)
{
    ESP_RETURN_ON_FALSE(handle && out && samples > 0, ESP_ERR_INVALID_ARG,
                        TAG, "参数为空或 samples=0");

    int64_t sum = 0;
    int valid = 0;
    for (int i = 0; i < samples; i++) {
        int raw;
        esp_err_t ret = adc_oneshot_read(handle->adc_handle,
                                         handle->config.adc_channel, &raw);
        if (ret == ESP_OK) {
            sum += raw;
            valid++;
        }
        if (i < samples - 1) vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (valid == 0) {
        ESP_LOGE(TAG, "ADC 采样全部失败");
        return ESP_FAIL;
    }
    data_from_raw(handle, (int)(sum / valid), out);
    return ESP_OK;
}
