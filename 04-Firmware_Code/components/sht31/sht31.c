/*
 * @file sht31.c
 * @brief SHT31 (Sensirion) I2C 温湿度传感器驱动实现
 *
 * 数据手册: Sensirion SHT3x-DIS (v1.0)
 * 参考: ESP-IDF v5.5 examples/peripherals/i2c/i2c_basic
 *
 * 设计思路：
 * - 所有命令都是 16 位，分成两个字节（MSB 在前）通过 I2C 发送
 * - 测量结果 6 字节：温度(2B) + CRC(1B) + 湿度(2B) + CRC(1B)
 * - 状态寄存器 3 字节：数据(2B) + CRC(1B)
 * - CRC 多项式 0x31 用于检测通信噪声，防止误用损坏的数据
 */

#include <string.h>
#include "sht31.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "sht31";

/* ===================================================================
 *  命令构造宏
 * ===================================================================
 * SHT31 的每条命令都是 16 位（2 字节），发送时先发高位字节，再发低位字节。
 * CMD(cmd)    将 16 位命令拆成两个 8 位数值，适合初始化数组
 * CMD_WORD(cmd)  单纯保留原值，用于位运算
 */
#define CMD(cmd)          ((cmd) >> 8), ((cmd) & 0xFF)
#define CMD_WORD(cmd)     (cmd)

/* ===================================================================
 *  命令常量（数据手册 Table 4 & 5）
 * ===================================================================
 * 命名规则：
 *   CMD_MEAS_CLK_xx  — 带时钟延延的测量（传感器测完才释放 SCL）
 *   CMD_MEAS_POLL_xx — 无时钟延延的测量（发完命令就返回）
 *   xx = HI / MED / LO 对应三级精度
 */

/* ---- 测量命令（时钟延展模式） ---- */
#define CMD_MEAS_CLK_HI   0x2C06   /* 高精度，传感器拉低 SCL 直到测量完成 */
#define CMD_MEAS_CLK_MED  0x2C0D   /* 中精度 */
#define CMD_MEAS_CLK_LO   0x2C10   /* 低精度 */

/* ---- 测量命令（轮询模式） ---- */
#define CMD_MEAS_POLL_HI  0x2400   /* 高精度，发完立刻返回，需等待后 fetch */
#define CMD_MEAS_POLL_MED 0x240B   /* 中精度 */
#define CMD_MEAS_POLL_LO  0x2416   /* 低精度 */

/* ---- 通用命令 ---- */
#define CMD_FETCH_DATA    0xE000   /* 轮询模式测量完成后，用此命令取回数据 */
#define CMD_READ_STATUS   0xF32D   /* 读状态寄存器 */
#define CMD_CLEAR_STATUS  0x3041   /* 清除状态寄存器中的可写标志位 */
#define CMD_SOFT_RESET    0x30A2   /* 软复位（不掉电，恢复默认状态） */
#define CMD_HEATER_EN     0x306D   /* 开启内部加热器（防结露） */
#define CMD_HEATER_DIS    0x3066   /* 关闭内部加热器 */

/* ===================================================================
 *  CRC-8 校验（数据手册 4.12 节）
 * ===================================================================
 * 特征多项式: x^8 + x^5 + x^4 + 1  →  0x31
 * 初始值:    0xFF
 * 最终异或:  无（0x00）
 *
 * 传感器每发 2 字节数据就附 1 字节 CRC，覆盖范围就是前 2 字节。
 * 校验失败说明 I2C 线路上有噪声干扰，数据不可信。
 */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;                      /* 初始值 */
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];                      /* 每进来一个字节先异或 */
        for (int j = 0; j < 8; j++) {        /* 逐位处理 */
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
            /* 最高位为 1 时左移后异或多项式，否则单纯左移 */
        }
    }
    return crc;
}

/* ===================================================================
 *  底层 I2C 通信辅助函数
 * =================================================================== */

/* 发送 16 位命令（无读取），用于复位/状态/加热器等只写操作 */
static esp_err_t write_cmd(sht31_handle_ptr_t h, uint16_t cmd)
{
    const uint8_t buf[] = { CMD(cmd) };
    return i2c_master_transmit(h->i2c_dev, buf, sizeof(buf), -1);
}

/* 发送命令并读取响应（I2C RESTART，不产生 STOP） */
static esp_err_t write_cmd_read(sht31_handle_ptr_t h, uint16_t cmd,
                                uint8_t *data, size_t len)
{
    const uint8_t buf[] = { CMD(cmd) };
    return i2c_master_transmit_receive(h->i2c_dev, buf, sizeof(buf),
                                       data, len, -1);
}

/* 逐组校验 CRC（每 2 字节数据 + 1 字节 CRC），测量结果含 2 组 */
static esp_err_t check_crcs(const uint8_t *raw, size_t len)
{
    for (size_t i = 0; i < len; i += 3) {
        uint8_t expected = crc8(&raw[i], 2);
        if (raw[i + 2] != expected) {
            ESP_LOGE(TAG, "CRC 校验失败: 第 %zu 字节组, 收到 0x%02X, 期望 0x%02X",
                     i, raw[i + 2], expected);
            return ESP_ERR_INVALID_CRC;
        }
    }
    return ESP_OK;
}

/* ===================================================================
 *  公共 API 实现
 * =================================================================== */

esp_err_t sht31_new(i2c_master_bus_handle_t bus_handle, uint8_t addr,
                    uint32_t scl_speed, sht31_handle_ptr_t *out_handle)
{
    esp_err_t ret;
    sht31_handle_ptr_t h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "内存不足，无法分配设备句柄");

    /* 配置 I2C 设备参数 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,   /* SHT31 使用 7 位地址 */
        .device_address  = addr,                  /* 用户传入的地址（0x44 或 0x45） */
        .scl_speed_hz    = scl_speed,             /* I2C 时钟频率 */
    };

    /* 将设备挂载到 I2C 总线上 */
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &h->i2c_dev);
    if (ret != ESP_OK) {
        free(h);
        return ret;
    }

    *out_handle = h;
    return ESP_OK;
}

esp_err_t sht31_del(sht31_handle_ptr_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "传入的句柄为空");

    /* 从 I2C 总线移除设备，然后释放内存 */
    esp_err_t ret = i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
    return ret;
}

esp_err_t sht31_measure(sht31_handle_ptr_t handle, sht31_repeat_t rep,
                        sht31_data_t *out)
{
    ESP_RETURN_ON_FALSE(handle && out, ESP_ERR_INVALID_ARG, TAG, "参数不能为空");

    /*
     * 时钟延展模式命令格式：0x2C + 精度码
     * 例：高精度 = 0x2C06 → 发送 {0x2C, 0x06}
     */
    uint16_t cmd = CMD_WORD(CMD_MEAS_CLK_HI & 0xFF00) | rep;  //
    uint8_t raw[6];

    /* 发命令并等待传感器完成测量（传感器会拉低 SCL 表示"正在忙"） */
    ESP_RETURN_ON_ERROR(write_cmd_read(handle, cmd, raw, sizeof(raw)),
                        TAG, "测量命令执行失败");

    /* 校验温度 CRC 和湿度 CRC */
    ESP_RETURN_ON_ERROR(check_crcs(raw, sizeof(raw)), TAG, "测量数据 CRC 校验失败");

    /* 组装原始数据并换算为物理量 */
    uint16_t raw_t = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t raw_h = ((uint16_t)raw[3] << 8) | raw[4];
    sht31_raw_to_units(raw_t, raw_h, &out->temperature, &out->humidity);
    return ESP_OK;
}

esp_err_t sht31_trigger(sht31_handle_ptr_t handle, sht31_repeat_t rep)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "传入的句柄为空");

    /*
     * 轮询模式命令格式：0x24 + 精度码
     * 此命令只触发测量，不等待结果。
     * 用户需等待对应精度的时间后调用 sht31_fetch() 取回数据。
     */
    uint16_t cmd = CMD_WORD(CMD_MEAS_POLL_HI & 0xFF00) | rep;
    return write_cmd(handle, cmd);
}

esp_err_t sht31_fetch(sht31_handle_ptr_t handle, sht31_data_t *out)
{
    ESP_RETURN_ON_FALSE(handle && out, ESP_ERR_INVALID_ARG, TAG, "参数不能为空");

    /* 发送"取数据"命令 (0xE000)，读取缓存在传感器内部的测量结果 */
    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(write_cmd_read(handle, CMD_FETCH_DATA, raw, sizeof(raw)),
                        TAG, "读取测量数据失败");
    ESP_RETURN_ON_ERROR(check_crcs(raw, sizeof(raw)), TAG, "测量数据 CRC 校验失败");

    uint16_t raw_t = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t raw_h = ((uint16_t)raw[3] << 8) | raw[4];
    sht31_raw_to_units(raw_t, raw_h, &out->temperature, &out->humidity);
    return ESP_OK;
}

esp_err_t sht31_read_status(sht31_handle_ptr_t handle, uint16_t *status)
{
    ESP_RETURN_ON_FALSE(handle && status, ESP_ERR_INVALID_ARG, TAG, "参数不能为空");

    /* 读状态寄存器返回 3 字节：高字节 + 低字节 + CRC */
    uint8_t raw[3];
    ESP_RETURN_ON_ERROR(write_cmd_read(handle, CMD_READ_STATUS, raw, sizeof(raw)),
                        TAG, "读取状态寄存器失败");
    ESP_RETURN_ON_ERROR(check_crcs(raw, 3), TAG, "状态寄存器 CRC 校验失败");

    *status = ((uint16_t)raw[0] << 8) | raw[1];
    return ESP_OK;
}

esp_err_t sht31_clear_status(sht31_handle_ptr_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "传入的句柄为空");
    return write_cmd(handle, CMD_CLEAR_STATUS);
}

esp_err_t sht31_soft_reset(sht31_handle_ptr_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "传入的句柄为空");
    return write_cmd(handle, CMD_SOFT_RESET);
}

esp_err_t sht31_heater_set(sht31_handle_ptr_t handle, bool on)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "传入的句柄为空");
    return write_cmd(handle, on ? CMD_HEATER_EN : CMD_HEATER_DIS);
}
