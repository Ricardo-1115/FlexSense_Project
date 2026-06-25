/*
 * FlexSense — 柔性传感器物联网节点
 * Copyright (C) 2026
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_sleep.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "cmd_system.h"
#include "cmd_nvs.h"
#include "battery.h"
#include "cmd_flexsense.h"
#include "ble_flexsense.h"
#include "ble_flexsense_svc.h"
#include "fsr402.h"
#include "sht31.h"

static const char *TAG = "FlexSense";
#define PROMPT_STR "FlexSense"

/* ── 低功耗阈值（与 LDO / ESP32-S3 / LiPo 匹配） ── */
#define PM_CHECK_INTERVAL_MS    2000        /* 电源管理检测周期 */
#define DEEP_SLEEP_SEC          30          /* 每次 deep sleep 时长 */
#define LOW_BATT_WARN_MV        3400        /* 低电量警告阈值（保留 BLE 连接） */
#define LOW_BATT_SLEEP_MV       3200        /* Deep Sleep 阈值 */
#define LOW_BATT_EXIT_MV        3300        /* 退出 Deep Sleep 阈值 */
#define PM_SETTLE_MS            1500        /* 上电后等待电源稳定再检测电池 */

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

/* 低电量警告标志 — 由电源管理任务维护，确保只通知一次 */
static bool s_batt_warned = false;

/* ── 电源管理任务：监测电池，分级响应 ── */
static void power_mgmt_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(PM_CHECK_INTERVAL_MS));

        uint32_t mv = battery_read_once_mv();
        if (mv == 0) continue;

        /* 低电量警告（带迟滞，避免反复通知） */
        if (mv < LOW_BATT_WARN_MV && !s_batt_warned) {
            flex_svc_set_low_battery(true);
            s_batt_warned = true;
            ESP_LOGI(TAG, "[PM] %"PRIu32"mV < %dmV，低电量警告", mv, LOW_BATT_WARN_MV);
        } else if (mv >= LOW_BATT_WARN_MV && s_batt_warned) {
            flex_svc_set_low_battery(false);
            s_batt_warned = false;
        }

        /* 低于休眠阈值 → Deep Sleep */
        if (mv < LOW_BATT_SLEEP_MV) {
            ESP_LOGI(TAG, "[PM] %"PRIu32"mV < %dmV，进入 Deep Sleep", mv, LOW_BATT_SLEEP_MV);
            flex_svc_set_low_power(true);
            vTaskDelay(pdMS_TO_TICKS(1500));
            ESP_LOGI(TAG, "[PM] 进入 Deep Sleep %ds", DEEP_SLEEP_SEC);
            esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_SEC * 1000000ULL);
            esp_deep_sleep_start();
        }
    }
}

/* ── 唤醒后检查电池，低于恢复阈值则继续睡 ──
 *  注意：等电源稳定后再读 ADC，不用 battery_init 时采的旧值。 */
static void check_battery_on_wakeup(void)
{
    vTaskDelay(pdMS_TO_TICKS(PM_SETTLE_MS));

    uint32_t mv = battery_read_once_mv();
    if (mv == 0) return;

    if (mv < LOW_BATT_EXIT_MV) {
        ESP_LOGI(TAG, "[PM] 唤醒后电池 %"PRIu32"mV < %dmV，继续睡眠 %ds",
                 mv, LOW_BATT_EXIT_MV, DEEP_SLEEP_SEC);
        esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_SEC * 1000000ULL);
        esp_deep_sleep_start();
    }

    ESP_LOGI(TAG, "[PM] 电池 %"PRIu32"mV >= %dmV，恢复运行", mv, LOW_BATT_EXIT_MV);
}

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    initialize_nvs();
    /* 初始化 I2C 主总线
     *
     * SHT31 温湿度传感器和后续其他 I2C 外设都共用这一条总线。
     * 引脚：GPIO38 = SDA（数据线），GPIO39 = SCL（时钟线）
     * 频率：100 kHz（SHT31 标准模式）
     *
     * 总线句柄 flexsense_i2c_bus 声明在 cmd_flexsense.h 中，
     * 各调试命令通过 extern 引用它，无需各自创建/销毁总线。
     */
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_38,
        .scl_io_num = GPIO_NUM_39,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &flexsense_i2c_bus));
    ESP_LOGI(TAG, "I2C 总线已初始化 (SDA=GPIO38, SCL=GPIO39)");

    /* ── 压力传感器 FSR402 (ADC1_CH9 / GPIO10, 30kΩ 分压 + TLV521) ── */
    ESP_ERROR_CHECK(fsr402_init(&fsr402_pcb_cfg, &flexsense_fsr));
    ESP_LOGI(TAG, "FSR402 已初始化 (GPIO10/ADC1_CH9, %"PRIu32"kΩ 分压)",
             fsr402_pcb_cfg.r_fixed / 1000);

    /* ── 电池电压监测 (ADC1_CH8/GPIO9, 100k+100k 分压) ── */
    battery_init(fsr402_get_adc_handle(flexsense_fsr));

    /* ── 唤醒后检查电池；仍低则继续 Deep Sleep（不进 BLE 初始化以省电） ── */
    check_battery_on_wakeup();

    /* 电源已稳定，预填电池滤波器，确保后续 battery_get_voltage_mv() 立即可用 */
    battery_prime_filter();

    /* ── 温湿度传感器 SHT31 (I2C 地址 0x44, 100kHz) ── */
    ESP_ERROR_CHECK(sht31_new(flexsense_i2c_bus, SHT31_ADDR_0, 100000, &flexsense_sht31));
    for (int i = 0; i < 3; i++) {
        sht31_data_t _d;
        if (sht31_measure(flexsense_sht31, SHT31_REPEAT_HIGH, &_d) == ESP_OK) {
            ESP_LOGI(TAG, "SHT31 已就绪: %.1f °C / %.1f %%RH",
                     _d.temperature, _d.humidity);
            break;
        }
        ESP_LOGW(TAG, "SHT31 通信超时，重试 %d/3...", i + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* ── BLE 初始化 ── */
    ESP_ERROR_CHECK(ble_flexsense_init());

    /* ── 电源管理任务（后台监控电池，低电量时 Deep Sleep） ── */
    xTaskCreatePinnedToCore(power_mgmt_task, "pm", 2048, NULL, 3, NULL, 1);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    esp_console_register_help_command();
    register_system_common();
#if SOC_LIGHT_SLEEP_SUPPORTED
    register_system_light_sleep();
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
    register_system_deep_sleep();
#endif
    register_nvs();
    register_flexsense();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#endif

    ESP_LOGI(TAG, "FlexSense v1.0 — 调试控制台就绪。输入 'help' 查看可用命令。");

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}