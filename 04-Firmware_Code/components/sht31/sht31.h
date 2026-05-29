#pragma once
/**
 * @file sht31.h
 * @brief SHT31 温湿度传感器驱动 — 用户接口（类型定义 & API 声明）
 *
 * 依赖 ESP-IDF v5.5 I2C master 驱动 (driver/i2c_master.h)。
 * 使用前需先初始化 I2C 主总线，再通过 sht31_new() 将本传感器挂载到总线上。
 */

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 硬件常量 ========== */

/** SHT31 的 7 位 I2C 地址（ADDR 引脚接低 → 0x44，接高 → 0x45） */
#define SHT31_ADDR_0     0x44
#define SHT31_ADDR_1     0x45

/* ========== 数据类型 ========== */

/** 一次测量得到的温湿度值 */
typedef struct {
    float temperature;  /*!< 温度，单位 °C */
    float humidity;     /*!< 相对湿度，单位 %RH */
} sht31_data_t;

/** 测量重复精度 —— 精度越高噪声越低，但转换耗时越长（~4 / 6 / 13 ms） */
typedef enum {
    SHT31_REPEAT_HIGH   = 0x06,  /*!< 高精度：噪声最低，耗时最长 */
    SHT31_REPEAT_MEDIUM = 0x0D,  /*!< 中等精度 */
    SHT31_REPEAT_LOW    = 0x10,  /*!< 低精度：速度最快，适合连续快速采样 */
} sht31_repeat_t;

/** SHT31 设备句柄（不透明指针，用户只需传递无需关心内部） */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;   /*!< ESP-IDF I2C 设备句柄 */
} sht31_handle_t;
typedef sht31_handle_t *sht31_handle_ptr_t;

/* ========== 设备生命周期 ========== */

/**
 * @brief 在已有的 I2C 总线上添加一个 SHT31 设备。
 *
 * @param[in]  bus_handle  已经初始化好的 I2C 主总线句柄
 * @param[in]  addr        设备地址，传 SHT31_ADDR_0 (0x44) 或 SHT31_ADDR_1 (0x45)
 * @param[in]  scl_speed   SCL 时钟频率，单位 Hz（SHT31 最高支持 1 MHz）
 * @param[out] out_handle  输出：新创建的设备句柄
 * @return ESP_OK 成功，否则失败
 */
esp_err_t sht31_new(i2c_master_bus_handle_t bus_handle, uint8_t addr,
                    uint32_t scl_speed, sht31_handle_ptr_t *out_handle);

/**
 * @brief 删除 SHT31 设备并释放内存。
 *
 * @param[in] handle  sht31_new() 返回的设备句柄
 * @return ESP_OK 成功
 */
esp_err_t sht31_del(sht31_handle_ptr_t handle);

/* ========== 测量操作 ========== */

/**
 * @brief 单次测量（时钟延展模式）—— 最常用的接口。
 *
 * 发送测量命令后，传感器会拉低 SCL 通知主机"正在测量"，
 * 测量完毕（~13 ms 以内）自动释放 SCL，主机接着读取结果。
 * 整个过程是阻塞的，但 FreeRTOS 调度器会出让 CPU 给其他任务。
 *
 * @param[in]  handle  设备句柄
 * @param[in]  rep     测量精度（SHT31_REPEAT_HIGH / _MEDIUM / _LOW）
 * @param[out] out     输出：温度 (°C) 和湿度 (%RH)
 * @return ESP_OK 成功，ESP_ERR_INVALID_CRC 数据校验失败
 */
esp_err_t sht31_measure(sht31_handle_ptr_t handle, sht31_repeat_t rep,
                        sht31_data_t *out);

/**
 * @brief 触发测量（非时钟延展模式）—— 适用于需要"点火即走"的场景。
 *
 * 仅发送测量命令，不等结果立即返回。
 * 调用者需要等待对应精度的时间后，再调用 sht31_fetch() 取回数据。
 * 适合在定时器中断或任务循环中配合使用，避免长时间阻塞 I2C 总线。
 *
 * @attention 触发后到 fetch 前的最小等待时间：
 *            高精度 ≈ 13 ms，中精度 ≈ 6 ms，低精度 ≈ 4 ms
 *
 * @param[in] handle  设备句柄
 * @param[in] rep     测量精度
 * @return ESP_OK 成功
 */
esp_err_t sht31_trigger(sht31_handle_ptr_t handle, sht31_repeat_t rep);

/**
 * @brief 取回之前触发的测量结果（配合 sht31_trigger 使用）。
 *
 * @param[in]  handle  设备句柄
 * @param[out] out     输出：温度 (°C) 和湿度 (%RH)
 * @return ESP_OK 成功，ESP_ERR_INVALID_CRC 数据校验失败
 */
esp_err_t sht31_fetch(sht31_handle_ptr_t handle, sht31_data_t *out);

/* ========== 传感器管理 ========== */

/**
 * @brief 读取状态寄存器（16 位）。
 *
 * 各标志位含义（数据手册 Table 7）：
 *   Bit 15 — 上次写入校验错误
 *   Bit 13 — 加热器状态（1=开）
 *   Bit 11 — 湿度告警触发
 *   Bit 10 — 温度告警触发
 *   Bit 4  — 系统复位已发生
 *   Bit 1  — 上次命令执行失败
 *
 * @param[in]  handle  设备句柄
 * @param[out] status  输出：16 位状态寄存器原始值
 * @return ESP_OK 成功
 */
esp_err_t sht31_read_status(sht31_handle_ptr_t handle, uint16_t *status);

/**
 * @brief 清除状态寄存器的可写位（复位标志、告警标志等）。
 *
 * 上电后状态寄存器中的"系统复位"位默认置 1，
 * 调用此函数可将其清零，便于后续监控是否有意外复位发生。
 */
esp_err_t sht31_clear_status(sht31_handle_ptr_t handle);

/**
 * @brief 软复位传感器（等效于重新上电，但更快）。
 *
 * 复位后传感器回到空闲状态，所有寄存器恢复默认值。
 * 复位完成后建议等待 1 ms 再发后续命令。
 */
esp_err_t sht31_soft_reset(sht31_handle_ptr_t handle);

/**
 * @brief 控制加热器开关。
 *
 * 加热器用于高湿环境防结露，或烘干被冷凝水浸润的传感器。
 * 开启后温度读数会受加热影响，不适合同时做精确测温。
 *
 * @param[in] handle  设备句柄
 * @param[in] on      true=开启，false=关闭
 * @return ESP_OK 成功
 */
esp_err_t sht31_heater_set(sht31_handle_ptr_t handle, bool on);

/* ========== 工具函数 ========== */

/**
 * @brief 将传感器原始读数（16 位无符号整数）换算为工程单位。
 *
 * 换算公式：
 *   温度 (°C)   = -45 + 175 × (raw_T / 65535)
 *   相对湿度 (%) = 100 × (raw_RH / 65535)
 *
 * @param[in]  raw_temp  温度原始值（0~65535）
 * @param[in]  raw_rh    湿度原始值（0~65535）
 * @param[out] temp_c    输出：摄氏温度
 * @param[out] rh_pct    输出：相对湿度百分比
 */
static inline void sht31_raw_to_units(uint16_t raw_temp, uint16_t raw_rh,
                                      float *temp_c, float *rh_pct)
{
    *temp_c = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *rh_pct = 100.0f  * ((float)raw_rh    / 65535.0f);
}

#ifdef __cplusplus
}
#endif
