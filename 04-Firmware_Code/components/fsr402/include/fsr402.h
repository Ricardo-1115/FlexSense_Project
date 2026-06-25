#pragma once
/**
 * @file fsr402.h
 * @brief FSR402 压敏电阻驱动 — 用户接口
 *
 * ========== 硬件怎么接 ==========
 *
 *   面包板: VCC_3V3 ── FSR402 ──┬── 10kΩ ── GND
 *                                       │
 *                                   GPIO10 (ADC1_CH9)
 *
 *   PCB:  VCC_3V3 ── FSR402 ──┬── 30kΩ ──┬── TLV521 运放 ── GPIO10
 *                                       │          │
 *                                       └──────────┘  (运放起缓冲作用，不改变分压值)
 *
 * ========== 数据怎么算出来的 ==========
 *
 *   ① ADC 读到电压 Vout
 *   ② 根据分压公式反推 FSR 电阻:  R_fsr = R_fixed × (Vcc − Vout) / Vout
 *   ③ 从电阻估算压力:             F = force_k / R_fsr
 *      (FSR402 的特性是"力越大电阻越小"，且电导 1/R 与力大致成正比)
 *
 * 依赖 ESP-IDF v5.5 ADC oneshot 驱动 (esp_adc/adc_oneshot.h)。
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 驱动配置 ========== */

/** 所有可调参数都集中在这里，改硬件时只需换一套配置 */
typedef struct {
    adc_channel_t  adc_channel;    /*!< ADC1 通道号，默认 ADC_CHANNEL_9 (GPIO10) */
    adc_bitwidth_t adc_bitwidth;   /*!< ADC 采样精度，默认 12 位 (0~4095) */
    adc_atten_t    adc_atten;      /*!< ADC 输入衰减，DB_12 表示可测 0~3.1V */
    uint32_t       v_ref_mv;       /*!< 电源电压 (mV)，默认 3300 mV */
    uint32_t       r_fixed;        /*!< 分压电阻 (Ω)，面包板 10000 / PCB 30000 */
    float          force_k;        /*!< 压力系数 (N·Ω)，默认 5000 */
} fsr402_config_t;

/** 面包板配置：10kΩ 下拉，无运放 */
extern const fsr402_config_t fsr402_breadboard_cfg;
/** PCB 配置：30kΩ 下拉 + TLV521 运放 */
extern const fsr402_config_t fsr402_pcb_cfg;

/* ========== 测量结果 ========== */

/** 一次读取能得到的所有信息 */
typedef struct {
    int      adc_raw;        /*!< ADC 原始值，范围 0~4095 */
    int      voltage_mv;     /*!< 分压中点电压 (mV)，有校准时用校准值 */
    uint32_t resistance;     /*!< FSR402 当前电阻 (Ω) */
    float    force_n;        /*!< 估算压力 (N)，0 表示无压力 */
    bool     is_pressed;     /*!< true = 检测到按压 (电阻 < 1MΩ) */
    uint64_t timestamp_us;   /*!< 采样时刻 (微秒) */
    bool     calibrated;     /*!< ADC 是否经过 eFuse 硬件校准 */
    bool     voltage_valid;  /*!< 电压读数是否可靠 (raw > 0) */
} fsr402_data_t;

/* ========== 设备句柄 ========== */

typedef struct fsr402_handle_t *fsr402_handle_ptr_t;

/* ========== API ========== */

/**
 * @brief 初始化 FSR402 驱动
 * @param[in]  cfg         硬件配置
 * @param[out] out_handle  输出：设备句柄
 * @return ESP_OK 成功，否则失败
 */
esp_err_t fsr402_init(const fsr402_config_t *cfg, fsr402_handle_ptr_t *out_handle);

/**
 * @brief 销毁 FSR402 驱动并释放资源
 * @param[in] handle  fsr402_init 返回的句柄
 * @return ESP_OK 成功
 */
esp_err_t fsr402_deinit(fsr402_handle_ptr_t handle);

/**
 * @brief 单次读取 FSR402（最快，但有噪声）
 * @param[in]  handle  设备句柄
 * @param[out] out     测量结果
 * @return ESP_OK 成功
 */
esp_err_t fsr402_read(fsr402_handle_ptr_t handle, fsr402_data_t *out);

/**
 * @brief 多次采样取平均（去噪声，更稳定）
 * @param[in]  handle   设备句柄
 * @param[in]  samples  采样次数，建议 5~20 次
 * @param[out] out      平均后的测量结果
 * @return ESP_OK 成功
 */
esp_err_t fsr402_read_avg(fsr402_handle_ptr_t handle, int samples, fsr402_data_t *out);

adc_oneshot_unit_handle_t fsr402_get_adc_handle(fsr402_handle_ptr_t handle);

#ifdef __cplusplus
}
#endif
