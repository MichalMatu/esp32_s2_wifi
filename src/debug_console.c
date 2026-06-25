#include "debug_console.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include "config_access.h"
#include "provisioning.h"
#include "status_led.h"
#include "wired_iface.h"
#include "wifi_profiles.h"

#define DEBUG_CONSOLE_UART ((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM)
#define DEBUG_CONSOLE_LINE_MAX 128

static const char *TAG = "debug_console";

static EventGroupHandle_t s_flags;
static int s_reconfigure_bit;
static bool s_started;
static volatile bool s_wifi_connected;
static volatile uint8_t s_last_disconnect_reason;
static volatile int8_t s_last_disconnect_rssi;
static volatile uint32_t s_wifi_to_usb_failures;
static volatile uint32_t s_usb_to_wifi_failures;

static void console_write(const char *text) {
    if (text == NULL) {
        return;
    }
    uart_write_bytes(DEBUG_CONSOLE_UART, text, strlen(text));
}

static void console_printf(const char *format, ...) {
    char buffer[384];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written <= 0) {
        return;
    }
    if ((size_t)written >= sizeof(buffer)) {
        buffer[sizeof(buffer) - 3] = '.';
        buffer[sizeof(buffer) - 2] = '.';
        buffer[sizeof(buffer) - 1] = '\0';
    }
    console_write(buffer);
}

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON:
        return "power on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "unknown";
    }
}

static const char *wifi_mode_name(wifi_mode_t mode) {
    switch (mode) {
    case WIFI_MODE_NULL:
        return "null";
    case WIFI_MODE_STA:
        return "sta";
    case WIFI_MODE_AP:
        return "ap";
    case WIFI_MODE_APSTA:
        return "apsta";
    default:
        return "unknown";
    }
}

static const char *disconnect_reason_name(uint8_t reason) {
    switch (reason) {
    case 0:
        return "none";
    case WIFI_REASON_ASSOC_LEAVE:
        return "assoc leave";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-way handshake timeout";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        return "group key update timeout";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "no AP found";
    case WIFI_REASON_AUTH_FAIL:
        return "auth failed";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection failed";
    case WIFI_REASON_ROAMING:
        return "roaming";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "no AP with compatible security";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "no AP in authmode threshold";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "no AP in RSSI threshold";
    default:
        return "unknown";
    }
}

static void format_uptime(char *out, size_t out_len) {
    uint64_t seconds = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t hours = seconds / 3600ULL;
    uint32_t minutes = (seconds % 3600ULL) / 60ULL;
    uint32_t secs = seconds % 60ULL;
    snprintf(out, out_len, "%" PRIu32 "h %" PRIu32 "m %" PRIu32 "s", hours, minutes, secs);
}

static void print_help(void) {
    console_write("Commands:\r\n"
                  "  help                 show this help\r\n"
                  "  status               system, heap, LED and bridge counters\r\n"
                  "  wifi                 station config and connection details\r\n"
                  "  scan                 start Wi-Fi scan and print results\r\n"
                  "  reconnect            disconnect and reconnect STA\r\n"
                  "  disconnect           disconnect STA\r\n"
                  "  profiles             list saved Wi-Fi profiles without passwords\r\n"
                  "  config [local|captive]\r\n"
                  "  led <off|on|status|identify>\r\n"
                  "  usb refresh         refresh USB NCM link state\r\n"
                  "  log <none|error|warn|info|debug|verbose>\r\n"
                  "  reprovision          restart into configuration mode\r\n"
                  "  download uart0       restart into ROM UART0 download mode\r\n"
                  "  reboot               restart now\r\n");
}

static void print_status(void) {
    char uptime[32];
    format_uptime(uptime, sizeof(uptime));

    EventBits_t bits = s_flags ? xEventGroupGetBits(s_flags) : 0;
    size_t core_addr = 0;
    size_t core_size = 0;
    esp_err_t core_ret = esp_core_dump_image_get(&core_addr, &core_size);

    console_printf("reset: %s (%d)\r\n", reset_reason_name(esp_reset_reason()), esp_reset_reason());
    console_printf("uptime: %s\r\n", uptime);
    console_printf("heap: free=%u min=%u largest=%u\r\n", (unsigned)esp_get_free_heap_size(),
                   (unsigned)esp_get_minimum_free_heap_size(),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    console_printf("psram: free=%u largest=%u\r\n",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    console_printf("led: mode=%s state=%s\r\n", status_led_get_mode_name(status_led_get_mode()),
                   status_led_get_state_name(status_led_get_state()));
    config_access_mode_t access_mode = config_access_get_mode();
    console_printf("config access: %s (%s)\r\n", config_access_mode_label(access_mode),
                   config_access_mode_name(access_mode));
    console_printf("events: 0x%08x\r\n", (unsigned)bits);
    console_printf("bridge failures: wifi->usb=%u usb->wifi=%u\r\n",
                   (unsigned)s_wifi_to_usb_failures, (unsigned)s_usb_to_wifi_failures);
    console_printf("last disconnect: reason=%u (%s) rssi=%d\r\n",
                   (unsigned)s_last_disconnect_reason,
                   disconnect_reason_name(s_last_disconnect_reason), (int)s_last_disconnect_rssi);
    if (core_ret == ESP_OK && core_size > 0) {
        console_printf("coredump: present addr=0x%08x size=%u\r\n", (unsigned)core_addr,
                       (unsigned)core_size);
    } else {
        console_printf("coredump: none (%s)\r\n", esp_err_to_name(core_ret));
    }
}

static void print_mac(const uint8_t mac[6]) {
    console_printf("%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void print_wifi(void) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret != ESP_OK) {
        console_printf("wifi mode: %s\r\n", esp_err_to_name(ret));
        return;
    }

    wifi_config_t config = {};
    ret = esp_wifi_get_config(WIFI_IF_STA, &config);
    console_printf("wifi mode: %s\r\n", wifi_mode_name(mode));
    if (ret == ESP_OK) {
        console_printf("sta ssid: %s\r\n",
                       config.sta.ssid[0] ? (const char *)config.sta.ssid : "<empty>");
    } else {
        console_printf("sta config: %s\r\n", esp_err_to_name(ret));
    }

    uint8_t mac[6] = {};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        console_write("sta mac: ");
        print_mac(mac);
        console_write("\r\n");
    }

    wifi_ap_record_t ap = {};
    ret = esp_wifi_sta_get_ap_info(&ap);
    console_printf("connected flag: %s\r\n", s_wifi_connected ? "yes" : "no");
    if (ret == ESP_OK) {
        char ssid[33] = {};
        memcpy(ssid, ap.ssid, sizeof(ssid) - 1);
        console_printf("ap: ssid=%s rssi=%d channel=%u auth=%s\r\n", ssid[0] ? ssid : "<hidden>",
                       (int)ap.rssi, (unsigned)ap.primary,
                       provisioning_wifi_auth_mode_name(ap.authmode));
        console_write("ap bssid: ");
        print_mac(ap.bssid);
        console_write("\r\n");
    } else {
        console_printf("ap: %s\r\n", esp_err_to_name(ret));
    }
}

static void print_scan_results(const provisioning_scan_snapshot_t *snapshot) {
    console_printf("scan: state=%s seq=%u total=%u shown=%u duration=%ums\r\n",
                   provisioning_scan_state_name(snapshot->state), (unsigned)snapshot->seq,
                   (unsigned)snapshot->total, (unsigned)snapshot->count,
                   (unsigned)snapshot->duration_ms);
    if (snapshot->state == PROVISIONING_SCAN_STATE_ERROR) {
        console_printf("scan error: %s\r\n", snapshot->error[0] ? snapshot->error : "unknown");
        return;
    }

    for (uint16_t i = 0; i < snapshot->count; i++) {
        char ssid[33] = {};
        memcpy(ssid, snapshot->records[i].ssid, sizeof(ssid) - 1);
        console_printf("%2u  ch=%2u  rssi=%4d  auth=%-10s  ssid=%s\r\n", (unsigned)(i + 1),
                       (unsigned)snapshot->records[i].primary, (int)snapshot->records[i].rssi,
                       provisioning_wifi_auth_mode_name(snapshot->records[i].authmode),
                       ssid[0] ? ssid : "<hidden>");
    }
}

static void run_scan(void) {
    provisioning_scan_snapshot_t *snapshot =
        heap_caps_calloc(1, sizeof(*snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (snapshot == NULL) {
        console_write("scan: no memory for snapshot\r\n");
        return;
    }

    console_write("scan: starting\r\n");
    esp_err_t ret = provisioning_scan_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        console_printf("scan start failed: %s\r\n", esp_err_to_name(ret));
    }

    for (int attempt = 0; attempt < 80; attempt++) {
        ret = provisioning_scan_get_snapshot(snapshot);
        if (ret == ESP_OK && snapshot->state != PROVISIONING_SCAN_STATE_RUNNING) {
            print_scan_results(snapshot);
            free(snapshot);
            return;
        }
        if (ret != ESP_OK) {
            console_printf("scan snapshot failed: %s\r\n", esp_err_to_name(ret));
            free(snapshot);
            return;
        }
        if ((attempt % 4) == 0) {
            console_write(".");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    console_write("\r\nscan: timeout waiting for result\r\n");
    free(snapshot);
}

static void set_log_level(const char *level) {
    esp_log_level_t parsed;

    if (strcmp(level, "none") == 0) {
        parsed = ESP_LOG_NONE;
    } else if (strcmp(level, "error") == 0) {
        parsed = ESP_LOG_ERROR;
    } else if (strcmp(level, "warn") == 0) {
        parsed = ESP_LOG_WARN;
    } else if (strcmp(level, "info") == 0) {
        parsed = ESP_LOG_INFO;
    } else if (strcmp(level, "debug") == 0) {
        parsed = ESP_LOG_DEBUG;
    } else if (strcmp(level, "verbose") == 0) {
        parsed = ESP_LOG_VERBOSE;
    } else {
        console_write("usage: log <none|error|warn|info|debug|verbose>\r\n");
        return;
    }

    esp_log_level_set("*", parsed);
    console_printf("log: level set to %s\r\n", level);
}

static void set_config_access(const char *mode) {
    if (mode == NULL) {
        config_access_mode_t access_mode = config_access_get_mode();
        console_printf("config access: %s (%s)\r\n", config_access_mode_label(access_mode),
                       config_access_mode_name(access_mode));
        return;
    }

    esp_err_t ret = config_access_set_mode_from_name(mode, true);
    if (ret != ESP_OK) {
        console_write("usage: config [local|captive]\r\n");
        return;
    }

    config_access_mode_t access_mode = config_access_get_mode();
    console_printf("config access saved: %s (%s)\r\n", config_access_mode_label(access_mode),
                   config_access_mode_name(access_mode));
    if (access_mode == CONFIG_ACCESS_MODE_CAPTIVE) {
        console_write("warning: captive can route Mac traffic to ESP in configuration mode\r\n");
    } else {
        console_write("local-only keeps Mac Wi-Fi online in configuration mode\r\n");
    }
    console_write("change applies next time configuration mode starts\r\n");
}

static void print_profiles(void) {
    wifi_profile_t profiles[WIFI_PROFILE_MAX];
    size_t count = 0;
    esp_err_t ret = wifi_profiles_load(profiles, WIFI_PROFILE_MAX, &count);
    if (ret != ESP_OK) {
        console_printf("profiles: %s\r\n", esp_err_to_name(ret));
        return;
    }
    if (count == 0) {
        console_write("profiles: none\r\n");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        console_printf("%u: %s\r\n", (unsigned)i, profiles[i].ssid);
    }
}

static void enter_download_mode(void) {
    console_write("download: restarting into ROM UART0 download mode\r\n");
    uart_wait_tx_done(DEBUG_CONSOLE_UART, pdMS_TO_TICKS(200));
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

static void run_command(char *line) {
    char *cursor = trim(line);
    if (*cursor == '\0') {
        return;
    }

    char *saveptr = NULL;
    const char *command = strtok_r(cursor, " ", &saveptr);
    const char *arg = strtok_r(NULL, " ", &saveptr);
    if (command == NULL) {
        return;
    }

    if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        print_help();
    } else if (strcmp(command, "status") == 0) {
        print_status();
    } else if (strcmp(command, "wifi") == 0) {
        print_wifi();
    } else if (strcmp(command, "scan") == 0) {
        run_scan();
    } else if (strcmp(command, "reconnect") == 0) {
        (void)esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_err_t ret = esp_wifi_connect();
        console_printf("reconnect: %s\r\n", esp_err_to_name(ret));
    } else if (strcmp(command, "disconnect") == 0) {
        esp_err_t ret = esp_wifi_disconnect();
        console_printf("disconnect: %s\r\n", esp_err_to_name(ret));
    } else if (strcmp(command, "profiles") == 0) {
        print_profiles();
    } else if (strcmp(command, "config") == 0) {
        set_config_access(arg);
    } else if (strcmp(command, "led") == 0) {
        if (arg == NULL) {
            console_write("usage: led <off|on|status|identify>\r\n");
            return;
        }
        esp_err_t ret = status_led_set_mode_from_name(arg, strcmp(arg, "identify") != 0);
        console_printf("led: %s\r\n", esp_err_to_name(ret));
    } else if (strcmp(command, "usb") == 0) {
        if (arg == NULL || strcmp(arg, "refresh") != 0) {
            console_write("usage: usb refresh\r\n");
            return;
        }
        esp_err_t ret = wired_usb_refresh_link();
        console_printf("usb refresh: %s\r\n", esp_err_to_name(ret));
    } else if (strcmp(command, "log") == 0) {
        if (arg == NULL) {
            console_write("usage: log <none|error|warn|info|debug|verbose>\r\n");
            return;
        }
        set_log_level(arg);
    } else if (strcmp(command, "reprovision") == 0) {
        if (s_flags) {
            xEventGroupSetBits(s_flags, s_reconfigure_bit);
            console_write("reprovision: requested\r\n");
        } else {
            console_write("reprovision: event group unavailable\r\n");
        }
    } else if (strcmp(command, "download") == 0) {
        if (arg == NULL || strcmp(arg, "uart0") != 0) {
            console_write("usage: download uart0\r\n");
            console_write("note: ROM UART0 uses GPIO43 TX / GPIO44 RX, not GPIO37/GPIO39\r\n");
            return;
        }
        enter_download_mode();
    } else if (strcmp(command, "reboot") == 0) {
        console_write("rebooting\r\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        console_write("unknown command, type help\r\n");
    }
}

static void prompt(void) {
    console_write("\r\nesp32> ");
}

static void debug_console_task(void *arg) {
    (void)arg;

    char line[DEBUG_CONSOLE_LINE_MAX];
    size_t line_len = 0;
    console_write("\r\nDebug console ready. Type help.\r\n");
    prompt();

    while (true) {
        uint8_t ch = 0;
        int received = uart_read_bytes(DEBUG_CONSOLE_UART, &ch, 1, pdMS_TO_TICKS(250));
        if (received <= 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            console_write("\r\n");
            line[line_len] = '\0';
            run_command(line);
            line_len = 0;
            prompt();
            continue;
        }

        if (ch == '\b' || ch == 0x7f) {
            if (line_len > 0) {
                line_len--;
                console_write("\b \b");
            }
            continue;
        }

        if (isprint((unsigned char)ch)) {
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = (char)ch;
                uart_write_bytes(DEBUG_CONSOLE_UART, (const char *)&ch, 1);
            } else {
                console_write("\a");
            }
        }
    }
}

esp_err_t debug_console_start(EventGroupHandle_t flags, int reconfigure_bit) {
    if (s_started) {
        return ESP_OK;
    }

    s_flags = flags;
    s_reconfigure_bit = reconfigure_bit;

    uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (!uart_is_driver_installed(DEBUG_CONSOLE_UART)) {
        ESP_RETURN_ON_ERROR(uart_driver_install(DEBUG_CONSOLE_UART, 1024, 0, 0, NULL, 0), TAG,
                            "Cannot install console UART driver");
    }
    ESP_RETURN_ON_ERROR(uart_param_config(DEBUG_CONSOLE_UART, &uart_config), TAG,
                        "Cannot configure console UART");
    ESP_RETURN_ON_ERROR(uart_set_pin(DEBUG_CONSOLE_UART, CONFIG_ESP_CONSOLE_UART_TX_GPIO,
                                     CONFIG_ESP_CONSOLE_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "Cannot set console UART pins");

    BaseType_t task_created = xTaskCreate(debug_console_task, "debug_console", 4096, NULL, 2, NULL);
    if (task_created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "Debug console started on UART%d TX=%d RX=%d", CONFIG_ESP_CONSOLE_UART_NUM,
             CONFIG_ESP_CONSOLE_UART_TX_GPIO, CONFIG_ESP_CONSOLE_UART_RX_GPIO);
    return ESP_OK;
}

void debug_console_set_wifi_connected(bool connected) {
    s_wifi_connected = connected;
}

void debug_console_record_disconnect(uint8_t reason, int8_t rssi) {
    s_last_disconnect_reason = reason;
    s_last_disconnect_rssi = rssi;
}

void debug_console_count_wifi_to_usb_failure(void) {
    s_wifi_to_usb_failures++;
}

void debug_console_count_usb_to_wifi_failure(void) {
    s_usb_to_wifi_failures++;
}
