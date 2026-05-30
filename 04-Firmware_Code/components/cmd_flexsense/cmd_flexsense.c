/*
 * FlexSense — 硬件调试命令 (GPIO / I2C / ADC / SHT31 / FSR402)
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "sht31.h"
#include "fsr402.h"

static const char *TAG = "cmd_flexsense";

/* 全局传感器句柄 — 由 main.c 初始化，各模块共用 */
i2c_master_bus_handle_t flexsense_i2c_bus = NULL;
fsr402_handle_ptr_t     flexsense_fsr     = NULL;
sht31_handle_ptr_t      flexsense_sht31    = NULL;

/* ========== gpio <pin> --set <0|1> | --get ========== */
static struct {
    struct arg_int *pin;
    struct arg_int *set;
    struct arg_int *get;
    struct arg_end *end;
} gpio_args;

static int cmd_gpio(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&gpio_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, gpio_args.end, argv[0]);
        return 1;
    }

    int pin = gpio_args.pin->ival[0];

    if (gpio_args.get->count > 0 && gpio_args.get->ival[0]) {
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        int level = gpio_get_level(pin);
        printf("GPIO%d = %d\n", pin, level);
        return 0;
    }

    if (gpio_args.set->count > 0) {
        int level = gpio_args.set->ival[0];
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, level);
        printf("GPIO%d 已设置为 %d\n", pin, level);
        return 0;
    }

    return 0;
}

static void register_gpio(void)
{
    gpio_args.pin = arg_int1(NULL, NULL, "<pin>", "GPIO 编号");
    gpio_args.set = arg_int0(NULL, "set", "<0|1>", "设置 GPIO 电平");
    gpio_args.get = arg_int0(NULL, "get", NULL, "读取 GPIO 电平");
    gpio_args.end = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "gpio",
        .help = "GPIO 读写调试",
        .hint = "<pin> [--set <0|1> | --get 1]",
        .func = &cmd_gpio,
        .argtable = &gpio_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ========== i2c_scan — 扫描 I2C 总线 ========== */
static int cmd_i2c_scan(int argc, char **argv)
{
    if (flexsense_i2c_bus == NULL) {
        ESP_LOGE(TAG, "I2C 总线未初始化");
        return 1;
    }

    printf("正在扫描 I2C 总线 (SDA=GPIO38, SCL=GPIO39)...\n");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    int dev_count = 0;
    for (int addr = 0; addr < 0x80; addr++) {
        i2c_master_dev_handle_t dev_handle;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };

        if (addr % 16 == 0) {
            printf("%02x ", addr);
        }

        esp_err_t err = i2c_master_bus_add_device(flexsense_i2c_bus, &dev_cfg, &dev_handle);
        if (err == ESP_OK) {
            uint8_t tx_data = 0x00;
            err = i2c_master_transmit(dev_handle, &tx_data, 1, 100);
            i2c_master_bus_rm_device(dev_handle);

            if (err == ESP_OK) {
                printf("%02x ", addr);
                dev_count++;
            } else {
                printf("-- ");
            }
        } else {
            printf("-- ");
        }

        if (addr % 16 == 15) {
            printf("\n");
        }
    }

    printf("发现 %d 个设备\n", dev_count);
    return 0;
}

static void register_i2c_scan(void)
{
    const esp_console_cmd_t cmd = {
        .command = "i2c_scan",
        .help = "扫描 I2C 总线设备地址",
        .hint = NULL,
        .func = &cmd_i2c_scan,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ========== adc <channel> — 读取 ADC 原始值 ========== */
static struct {
    struct arg_int *channel;
    struct arg_end *end;
} adc_args;

static int cmd_adc(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&adc_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, adc_args.end, argv[0]);
        return 1;
    }

    int ch = adc_args.channel->ival[0];
    if (ch < 0 || ch > 9) {
        printf("通道号需在 0-9 范围内\n");
        return 1;
    }

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ch, &chan_cfg));

    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ch, &raw));
    printf("ADC1_CH%d = %d (raw, 12-bit)\n", ch, raw);

    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    return 0;
}

static void register_adc(void)
{
    adc_args.channel = arg_int1(NULL, NULL, "<channel>", "ADC 通道号 (0-9)");
    adc_args.end = arg_end(1);
    const esp_console_cmd_t cmd = {
        .command = "adc",
        .help = "读取 ADC1 通道原始值",
        .hint = "<0-9>",
        .func = &cmd_adc,
        .argtable = &adc_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ===================================================================
 *  sht31 命令 — SHT31 温湿度传感器调试入口
 * ===================================================================
 * 用法:
 *   sht31 read             — 测量温湿度（时钟延展模式，约等 13 ms）
 *   sht31 status           — 查看传感器健康状态（加热器/告警/复位标志等）
 *   sht31 clear            — 清除状态寄存器中的标志位
 *   sht31 reset            — 软复位传感器（等效重上电）
 *   sht31 heater on|off    — 控制防结露加热器
 *
 * 注意：
 *   SHT31 句柄由 main.c 在启动时一次性初始化，调试命令直接复用。
 *   I2C 总线同样由 main.c 统一管理。详情见 cmd_flexsense.h。
 */

/* 子命令参数表 */
static struct {
    struct arg_str *cmd;        /* 子命令名: read / status / clear / reset / heater */
    struct arg_str *heater;     /* 加热器参数: on 或 off（仅 heater 子命令需要） */
    struct arg_end *end;       /* argtable3 结束标记 */
} sht31_args;

static int cmd_sht31(int argc, char **argv)
{
    /* 检查 I2C 总线是否已在 main.c 中初始化好 */
    if (flexsense_i2c_bus == NULL) {
        ESP_LOGE(TAG, "I2C 总线未初始化");
        return 1;
    }
    if (flexsense_sht31 == NULL) {
        ESP_LOGE(TAG, "SHT31 未初始化（main.c 中初始化失败）");
        return 1;
    }

    /* 解析命令行参数 */
    int nerrors = arg_parse(argc, argv, (void **)&sht31_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, sht31_args.end, argv[0]);
        return 1;
    }

    const char *sub = sht31_args.cmd->sval[0];

    /* ---- 子命令分发 ---- */
    if (strcmp(sub, "read") == 0) {
        sht31_data_t data;
        esp_err_t err = sht31_measure(flexsense_sht31, SHT31_REPEAT_HIGH, &data);
        if (err == ESP_OK) {
            printf("温度: %.2f °C\n", data.temperature);
            printf("湿度: %.2f %%RH\n", data.humidity);
        } else {
            ESP_LOGE(TAG, "测量失败: %s", esp_err_to_name(err));
        }

    } else if (strcmp(sub, "status") == 0) {
        uint16_t status;
        esp_err_t err = sht31_read_status(flexsense_sht31, &status);
        if (err == ESP_OK) {
            printf("状态寄存器: 0x%04X\n", status);
            printf("  写入校验:     %s\n", (status & (1 << 15)) ? "上次写入有误" : "正常");
            printf("  加热器:       %s\n", (status & (1 << 13)) ? "开启" : "关闭");
            printf("  湿度告警:     %s\n", (status & (1 << 11)) ? "触发" : "正常");
            printf("  温度告警:     %s\n", (status & (1 << 10)) ? "触发" : "正常");
            printf("  系统已复位:   %s\n", (status & (1 << 4))  ? "是" : "否");
            printf("  上次命令:     %s\n", (status & (1 << 1))  ? "失败" : "成功");
        } else {
            ESP_LOGE(TAG, "读状态寄存器失败: %s", esp_err_to_name(err));
        }

    } else if (strcmp(sub, "clear") == 0) {
        esp_err_t err = sht31_clear_status(flexsense_sht31);
        if (err == ESP_OK) {
            printf("状态寄存器已清除（可写位归零）\n");
        } else {
            ESP_LOGE(TAG, "清除状态失败: %s", esp_err_to_name(err));
        }

    } else if (strcmp(sub, "reset") == 0) {
        esp_err_t err = sht31_soft_reset(flexsense_sht31);
        if (err == ESP_OK) {
            printf("SHT31 软复位完成\n");
        } else {
            ESP_LOGE(TAG, "复位失败: %s", esp_err_to_name(err));
        }

    } else if (strcmp(sub, "heater") == 0) {
        if (sht31_args.heater->count == 0) {
            printf("用法: sht31 heater <on|off>\n");
            return 1;
        }
        bool on = strcmp(sht31_args.heater->sval[0], "on") == 0;
        esp_err_t err = sht31_heater_set(flexsense_sht31, on);
        if (err == ESP_OK) {
            printf("加热器已%s\n", on ? "开启" : "关闭");
        } else {
            ESP_LOGE(TAG, "加热器控制失败: %s", esp_err_to_name(err));
        }

    } else {
        printf("未知子命令: %s\n可用: read, status, clear, reset, heater\n", sub);
        return 1;
    }

    return 0;
}

static void register_sht31(void)
{
    sht31_args.cmd    = arg_str1(NULL, NULL, "<read|status|clear|reset|heater>", "子命令");
    sht31_args.heater = arg_str0(NULL, NULL, "<on|off>",               "加热器开关 (仅 heater 子命令)");
    sht31_args.end    = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "sht31",
        .help = "SHT31 温湿度传感器调试",
        .hint = "<read|status|clear|reset|heater> [<on|off>]",
        .func = &cmd_sht31,
        .argtable = &sht31_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ===================================================================
 *  fsr — FSR402 压敏电阻调试命令
 * ===================================================================
 * 用法:
 *   fsr read              — 单次读取 FSR402
 *   fsr read <n>          — 连续采样 n 次取平均
 *   fsr config            — 查看当前配置
 *   fsr config breadboard — 切换到面包板配置（10kΩ + 无运放）
 *   fsr config pcb        — 切换到 PCB 配置（30kΩ + TLV521 运放）
 *
 * FSR402 句柄由 main.c 在启动时一次性初始化，调试命令直接复用。
 * fsr config 命令可切换硬件配置（会重建句柄）。
 * 连续调试时建议多采几次平均（如 fsr read 10）。
 */

/* 当前选中的配置（默认面包板，fsr config 命令可切换） */
static const fsr402_config_t *fsr_current_cfg = &fsr402_breadboard_cfg;

static struct {
    struct arg_str *cmd;
    struct arg_int *samples;
    struct arg_str *cfg;
    struct arg_end *end;
} fsr_args;

static int cmd_fsr(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&fsr_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, fsr_args.end, argv[0]);
        return 1;
    }

    const char *sub = fsr_args.cmd->sval[0];

    /* ---- fsr config [breadboard|pcb] — 切换配置（重建全局句柄） ---- */
    if (strcmp(sub, "config") == 0) {
        if (fsr_args.cfg->count > 0) {
            const char *mode = fsr_args.cfg->sval[0];
            const fsr402_config_t *new_cfg;

            if (strcmp(mode, "breadboard") == 0) {
                new_cfg = &fsr402_breadboard_cfg;
            } else if (strcmp(mode, "pcb") == 0) {
                new_cfg = &fsr402_pcb_cfg;
            } else {
                printf("未知配置: %s\n可用: breadboard, pcb\n", mode);
                return 1;
            }

            /* 销毁旧句柄，用新配置重建 */
            fsr402_deinit(flexsense_fsr);
            flexsense_fsr = NULL;
            esp_err_t err = fsr402_init(new_cfg, &flexsense_fsr);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "FSR402 重初始化失败: %s", esp_err_to_name(err));
                return 1;
            }
            fsr_current_cfg = new_cfg;
            printf("已切换至 %s (R_fixed = %" PRIu32 " Ω)\n",
                   mode, fsr_current_cfg->r_fixed);

        } else {
            uint32_t r = fsr_current_cfg->r_fixed;
            printf("--- FSR402 当前配置 ---\n");
            printf("  分压电阻: %" PRIu32 " Ω\n", r);
            printf("  运放:     %s\n", (r == 10000) ? "无" : "TLV521");
            printf("  硬件:     %s\n", (r == 10000) ? "面包板" : "PCB");
        }
        return 0;
    }

    /* ---- fsr read [samples] — 读取 FSR402 ---- */

    /* 检查全局句柄 */
    if (flexsense_fsr == NULL) {
        ESP_LOGE(TAG, "FSR402 未初始化（main.c 中初始化失败）");
        return 1;
    }

    int samples = 1;
    if (fsr_args.samples->count > 0) {
        samples = fsr_args.samples->ival[0];
        if (samples < 1) samples = 1;
        if (samples > 100) samples = 100;
    }

    fsr402_data_t data;
    esp_err_t err;
    if (samples > 1) {
        err = fsr402_read_avg(flexsense_fsr, samples, &data);
    } else {
        err = fsr402_read(flexsense_fsr, &data);
    }

    if (err == ESP_OK) {
        printf("--- FSR402 读数 [%s] ---\n",
               (fsr_current_cfg->r_fixed == 10000) ? "面包板" : "PCB");
        printf("  GPIO10(ADC1_CH9)");
        if (data.calibrated) printf(" [已校准]");
        else                 printf(" [未校准，线性估算]");
        printf("\n");
        printf("  ADC 原始值:    %d (0~4095)\n", data.adc_raw);
        printf("  中点电压:      %d mV\n", data.voltage_mv);
        printf("  FSR 电阻:      ");
        if (data.resistance == UINT32_MAX) {
            printf("开路 (>10 MΩ)\n");
        } else if (data.resistance > 1000000) {
            printf("%.2f MΩ\n", data.resistance / 1e6f);
        } else if (data.resistance >= 1000) {
            printf("%.2f kΩ\n", data.resistance / 1e3f);
        } else {
            printf("%" PRIu32 " Ω\n", data.resistance);
        }
        printf("  估算压力:      %.3f N", data.force_n);
        if (data.is_pressed) {
            printf(" [已检测到按压]");
        } else {
            printf(" [无按压]");
        }
        printf("\n");
        if (samples > 1) {
            printf("  (采样次数: %d 次取平均)\n", samples);
        }
    } else {
        ESP_LOGE(TAG, "读取 FSR402 失败: %s", esp_err_to_name(err));
    }

    return 0;
}

static void register_fsr(void)
{
    fsr_args.cmd     = arg_str1(NULL, NULL, "<read|config>", "子命令: read 或 config");
    fsr_args.samples = arg_int0("n", NULL, "<1-100>", "采样次数 (仅 read 子命令，默认 1)");
    fsr_args.cfg     = arg_str0(NULL, NULL, "<breadboard|pcb>", "配置模式 (仅 config 子命令)");
    fsr_args.end     = arg_end(3);
    const esp_console_cmd_t cmd = {
        .command = "fsr",
        .help = "FSR402 压敏电阻调试",
        .hint = "<read [n]|config [breadboard|pcb]>",
        .func = &cmd_fsr,
        .argtable = &fsr_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ========== 注册所有 FlexSense 命令 ========== */
void register_flexsense(void)
{
    register_gpio();
    register_i2c_scan();
    register_adc();
    register_sht31();
    register_fsr();
}
