/*
 * @file fsr402.c
 * @brief FSR402 (Interlink) 压敏电阻 ADC 驱动实现
 *
 * 数据手册: Interlink FSR402 (Integration Guide v1.0)
 * 参考: ESP-IDF v5.5 examples/peripherals/adc/oneshot_read
 *
 * ========== 数据链路 ==========
 *
 *   FSR402  ──→ 分压电路 ──→ ADC 采样 ──→ 电压校准 ──→ 算电阻 ──→ 算压力
 *   (可变电阻)   (R_fixed)   (GPIO10)    (eFuse/线性)  (分压公式)  (F = k/R)
 *
 * ========== 原理回顾 ==========
 *
 *   分压电路：
 *     Vout = Vcc × R_fixed / (R_fsr + R_fixed)
 *     → R_fsr = R_fixed × (Vcc − Vout) / Vout
 *
 *   压力估算（FSR402 典型特性）：
 *     电导 G = 1/R_fsr 与力 F 在 log-log 坐标下近似成正比（斜率 ≈ 1），
 *     所以 F ≈ force_k / R_fsr
 *
 *     force_k = 5000 的来源：
 *       数据手册典型曲线上，1N 压力时 FSR402 电阻 ≈ 5000 Ω，
 *       反推: force_k = F × R = 1N × 5000Ω = 5000 N·Ω
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
    adc_oneshot_unit_handle_t adc_handle;   /* ADC1 oneshot 句柄 */
    adc_cali_handle_t         cali_handle;  /* 校准句柄，NULL = 无校准 */
    fsr402_config_t           config;       /* 用户传入的配置副本 */
    bool                      calibrated;   /* 校准初始化是否成功 */
};

/* ===================================================================
 *  ADC 校准初始化
 * ===================================================================
 * ESP32-S3 用 LINE_FITTING 方案，需要芯片 eFuse 里烧录了电压基准。
 * 如果 eFuse 是空的（工程样片常见），校准失败，改用线性估算。
 */

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten,
                                 adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGD(TAG, "calibration scheme: Line Fitting");
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC 校准成功");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse 未烧录，跳过软件校准，将使用线性估算电压");
    } else {
        ESP_LOGE(TAG, "ADC 校准初始化失败 (%s)", esp_err_to_name(ret));
    }
    return calibrated;
}

/* ===================================================================
 *  预定义配置
 * ===================================================================
 * 这里放两套常用配置，用户也可以自己构造 fsr402_config_t。
 *
 * force_k = 5000 的来历：
 *   根据 FSR402 数据手册的 Force vs Resistance 典型曲线，
 *   1N 的力大约让电阻降到 5kΩ。因为 F = force_k / R，
 *   所以 force_k = 1N × 5000Ω = 5000 N·Ω。
 *   这是一个粗略的平均值，不同批次/不同安装方式会有偏差。
 *   如果你有砝码，可以实测标定出你自己的 force_k。
 */

const fsr402_config_t fsr402_breadboard_cfg = {
    .adc_channel  = ADC_CHANNEL_9,      /* GPIO10 */
    .adc_bitwidth = ADC_BITWIDTH_12,
    .adc_atten    = ADC_ATTEN_DB_12,    /* 量程 0~3.1V */
    .v_ref_mv     = 3300,               /* VCC = 3.3V */
    .r_fixed      = 10000,              /* 面包板 10kΩ */
    .force_k      = 5000.0f,            /* 1N 对应 ~5kΩ */
};

const fsr402_config_t fsr402_pcb_cfg = {
    .adc_channel  = ADC_CHANNEL_9,
    .adc_bitwidth = ADC_BITWIDTH_12,
    .adc_atten    = ADC_ATTEN_DB_12,
    .v_ref_mv     = 3300,
    .r_fixed      = 30000,              /* PCB 使用 30kΩ */
    .force_k      = 5000.0f,
};

/* ===================================================================
 *  初始化 & 销毁
 * =================================================================== */

esp_err_t fsr402_init(const fsr402_config_t *cfg, fsr402_handle_ptr_t *out_handle)
{
    esp_err_t ret;
    fsr402_handle_ptr_t h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "内存不足，无法分配句柄");

    memcpy(&h->config, cfg, sizeof(*cfg));

    /* ---- 创建 ADC1 oneshot 单元 ---- */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ret = adc_oneshot_new_unit(&unit_cfg, &h->adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 初始化失败 (%s)", esp_err_to_name(ret));
        free(h);
        return ret;
    }

    /* ---- 配置 ADC 通道（衰减、位宽） ---- */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = cfg->adc_atten,
        .bitwidth = cfg->adc_bitwidth,
    };
    ret = adc_oneshot_config_channel(h->adc_handle, cfg->adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败 (%s)", esp_err_to_name(ret));
        adc_oneshot_del_unit(h->adc_handle);
        free(h);
        return ret;
    }

    /* ---- 尝试初始化电压校准（失败也不影响读取，只是电压没那么准） ---- */
    h->calibrated = adc_calibration_init(ADC_UNIT_1, cfg->adc_atten,
                                         &h->cali_handle);

    *out_handle = h;
    return ESP_OK;
}

esp_err_t fsr402_deinit(fsr402_handle_ptr_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "传入的句柄为空");

    if (handle->calibrated && handle->cali_handle) {
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle->cali_handle));
#endif
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(handle->adc_handle));
    free(handle);
    return ESP_OK;
}

/* ===================================================================
 *  数据转换（内部函数）
 * =================================================================== */

/**
 * raw_to_voltage — 把 ADC 读数变成电压
 *
 * 两个路径：
 *   ① 有 eFuse 校准 → 用 ESP-IDF 的校准函数 (最准)
 *   ② 无校准 → 线性估算: V = raw × V_ref / 4095 (够用)
 */
static int raw_to_voltage(fsr402_handle_ptr_t h, int raw)
{
    if (h->calibrated) {
        int voltage;
        esp_err_t ret = adc_cali_raw_to_voltage(h->cali_handle, raw, &voltage);
        if (ret == ESP_OK) {
            return voltage;
        }
        ESP_LOGW(TAG, "校准转换失败，回退到线性估算");
    }
    /* 线性估算：12 位 ADC 最大值 = 4095 */
    int max_raw = (1 << h->config.adc_bitwidth) - 1;
    return (int)((uint64_t)raw * h->config.v_ref_mv / max_raw);
}

/**
 * voltage_to_resistance — 从分压中点电压反推 FSR402 电阻
 *
 *   分压公式:  Vout = Vcc × R_fixed / (R_fsr + R_fixed)
 *   变形:       R_fsr = R_fixed × (Vcc − Vout) / Vout
 *
 *   无压力时 Vout ≈ 0V → 返回 UINT32_MAX（开路）
 *   压力极大时 Vout ≈ Vcc → 返回 0（短路）
 */
static uint32_t voltage_to_resistance(const fsr402_config_t *cfg, int voltage_mv)
{
    if (voltage_mv <= 0) {
        return UINT32_MAX;   /* 开路，无压力 */
    }
    if (voltage_mv >= (int)cfg->v_ref_mv) {
        return 0;            /* 短路，压力过大 */
    }
    /* 用 64 位中间结果防止溢出 */
    uint64_t num = (uint64_t)cfg->r_fixed * (cfg->v_ref_mv - voltage_mv);
    return (uint32_t)(num / voltage_mv);
}

/**
 * resistance_to_force — 从 FSR 电阻估算压力
 *
 *   FSR402 的典型特性：电导 1/R 与压力 F 近似成正比（log-log 斜率 ≈ 1）
 *   模型: F = force_k / R
 *
 *   如果 R 接近开路 (> 1MΩ) 或等于 0，直接返回 0（无有效压力）
 *   默认 force_k = 5000，即 R = 5kΩ 时 F ≈ 1N
 */
static float resistance_to_force(const fsr402_config_t *cfg, uint32_t resistance)
{
    if (resistance == 0 || resistance >= 1000000) {
        return 0.0f;  /* 短路或开路，都没有有效压力 */
    }
    return cfg->force_k / (float)resistance;
}

/* ===================================================================
 *  对外读取接口
 * =================================================================== */

esp_err_t fsr402_read(fsr402_handle_ptr_t handle, fsr402_data_t *out)
{
    ESP_RETURN_ON_FALSE(handle && out, ESP_ERR_INVALID_ARG, TAG, "参数不能为空");

    int raw;
    esp_err_t ret = adc_oneshot_read(handle->adc_handle,
                                     handle->config.adc_channel, &raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 读取失败 (%s)", esp_err_to_name(ret));
        return ret;
    }

    int voltage = raw_to_voltage(handle, raw);
    uint32_t resistance = voltage_to_resistance(&handle->config, voltage);
    float force = resistance_to_force(&handle->config, resistance);

    out->adc_raw       = raw;
    out->voltage_mv    = voltage;
    out->resistance    = resistance;
    out->force_n       = force;
    out->is_pressed    = (resistance < 1000000);
    out->timestamp_us  = esp_timer_get_time();
    out->calibrated    = handle->calibrated;
    out->voltage_valid = (raw > 0);

    return ESP_OK;
}

esp_err_t fsr402_read_avg(fsr402_handle_ptr_t handle, int samples,
                          fsr402_data_t *out)
{
    ESP_RETURN_ON_FALSE(handle && out, ESP_ERR_INVALID_ARG, TAG, "参数不能为空");
    ESP_RETURN_ON_FALSE(samples > 0, ESP_ERR_INVALID_ARG, TAG, "采样次数必须 > 0");

    /*
     * 多次采样取平均，降低 ADC 噪声。
     * 每次采样间隔 2ms，让 ADC 采样电容有足够时间恢复。
     * 只统计读取成功的样本，避免失败样本拉低平均值。
     */
    int64_t sum_raw = 0;
    int     valid   = 0;

    for (int i = 0; i < samples; i++) {
        int raw;
        esp_err_t ret = adc_oneshot_read(handle->adc_handle,
                                         handle->config.adc_channel, &raw);
        if (ret == ESP_OK) {
            sum_raw += raw;
            valid++;
        }
        if (i < samples - 1) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    /* 所有样本都失败 → 放弃 */
    if (valid == 0) {
        ESP_LOGE(TAG, "所有 ADC 采样均失败");
        return ESP_FAIL;
    }

    int avg_raw = (int)(sum_raw / valid);

    int voltage = raw_to_voltage(handle, avg_raw);
    uint32_t resistance = voltage_to_resistance(&handle->config, voltage);
    float force = resistance_to_force(&handle->config, resistance);

    out->adc_raw       = avg_raw;
    out->voltage_mv    = voltage;
    out->resistance    = resistance;
    out->force_n       = force;
    out->is_pressed    = (resistance < 1000000);
    out->timestamp_us  = esp_timer_get_time();
    out->calibrated    = handle->calibrated;
    out->voltage_valid = (avg_raw > 0);

    return ESP_OK;
}
