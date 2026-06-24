#include "diagnostics.h"

#include "diagnostics/input.h"
#include "diagnostics/oled.h"
#include "diagnostics/private.h"
#include "diagnostics/ui.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include <stdio.h>
#include <string.h>

#if CONFIG_DIAG_OLED_ENABLED

static const char *TAG = "diagnostics";
static const TickType_t DIAG_INPUT_POLL_TICKS = 1;

static diag_state_t s_state = {
    .mode = DIAG_MODE_BOOT,
};
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_display_ready;
static uint8_t s_oled_address;
static diag_input_state_t s_input_state;
static portMUX_TYPE s_input_lock = portMUX_INITIALIZER_UNLOCKED;

static void snapshot(diag_state_t *out)
{
    portENTER_CRITICAL(&s_state_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}

static void input_snapshot(diag_input_state_t *out)
{
    portENTER_CRITICAL(&s_input_lock);
    *out = s_input_state;
    portEXIT_CRITICAL(&s_input_lock);
}

static void oled_render_runtime(void)
{
    static int64_t last_render_ms;
    static uint32_t last_input_revision;
    int64_t now_ms = esp_timer_get_time() / 1000;
    diag_input_state_t input;

    input_snapshot(&input);
    bool input_changed = input.revision != last_input_revision;
    if (input_changed || now_ms - last_render_ms >= CONFIG_DIAG_OLED_UPDATE_MS) {
        diag_state_t state;
        snapshot(&state);
        diag_ui_render(&state, &input);
        last_render_ms = now_ms;
        last_input_revision = input.revision;
    }
}

static void set_scan_running(bool running, int error)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.wifi_scan_running = running;
    s_state.wifi_scan_error = error;
    if (running) {
        s_state.wifi_scan_count = 0;
        s_state.wifi_scan_total = 0;
    }
    portEXIT_CRITICAL(&s_state_lock);
}

static void publish_scan_results(const wifi_ap_record_t *records, uint16_t count, uint16_t total, int error)
{
    diag_wifi_scan_result_t results[DIAG_WIFI_SCAN_RESULTS] = {};

    if (count > DIAG_WIFI_SCAN_RESULTS) {
        count = DIAG_WIFI_SCAN_RESULTS;
    }

    for (uint16_t i = 0; i < count; i++) {
        snprintf(results[i].ssid,
                 sizeof(results[i].ssid),
                 "%.32s",
                 (const char *)records[i].ssid);
        results[i].rssi = records[i].rssi;
        results[i].channel = records[i].primary;
        results[i].auth_mode = records[i].authmode;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.wifi_scan_running = false;
    s_state.wifi_scan_error = error;
    s_state.wifi_scan_count = count;
    s_state.wifi_scan_total = total;
    memcpy(s_state.wifi_scan_results, results, sizeof(s_state.wifi_scan_results));
    portEXIT_CRITICAL(&s_state_lock);
}

static void scan_wifi(void)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };
    wifi_ap_record_t records[DIAG_WIFI_SCAN_RESULTS] = {};
    uint16_t total = 0;
    uint16_t count = DIAG_WIFI_SCAN_RESULTS;

    ESP_LOGI(TAG, "WiFi scan requested from OLED menu");
    set_scan_running(true, 0);

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        publish_scan_results(records, 0, 0, err);
        return;
    }

    esp_wifi_scan_get_ap_num(&total);
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan records failed: %s", esp_err_to_name(err));
        publish_scan_results(records, 0, total, err);
        return;
    }

    publish_scan_results(records, count, total, 0);
}

static void handle_action(diag_action_t action)
{
    switch (action) {
    case DIAG_ACTION_SCAN_WIFI:
        scan_wifi();
        break;
    case DIAG_ACTION_RECONNECT_WIFI:
        ESP_LOGI(TAG, "Reconnect WiFi requested from OLED menu");
        esp_wifi_disconnect();
        esp_wifi_connect();
        break;
    case DIAG_ACTION_RESTART_BRIDGE:
        ESP_LOGW(TAG, "Bridge restart requested from OLED menu");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        break;
    case DIAG_ACTION_RESTART_ESP:
        ESP_LOGW(TAG, "Restart requested from OLED menu");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        break;
    case DIAG_ACTION_BOOTLOADER:
        ESP_LOGW(TAG, "Bootloader requested from OLED menu");
        vTaskDelay(pdMS_TO_TICKS(250));
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
        break;
    case DIAG_ACTION_RESET_WIFI_CONFIG:
        ESP_LOGW(TAG, "WiFi config reset requested from OLED menu");
        esp_wifi_restore();
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        break;
    case DIAG_ACTION_FACTORY_RESET:
        ESP_LOGW(TAG, "Factory reset requested from OLED menu");
        nvs_flash_erase();
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        break;
    default:
        break;
    }
}

static void input_task(void *arg)
{
    (void)arg;
    diag_input_state_t local_input;

    input_snapshot(&local_input);
    while (true) {
        diag_input_poll(&local_input);

        diag_action_t requested_action = local_input.action_requested;
        portENTER_CRITICAL(&s_input_lock);
        s_input_state = local_input;
        portEXIT_CRITICAL(&s_input_lock);

        if (requested_action != DIAG_ACTION_NONE) {
            local_input.action_requested = DIAG_ACTION_NONE;
            vTaskDelay(pdMS_TO_TICKS(250));
            handle_action(requested_action);

            portENTER_CRITICAL(&s_input_lock);
            s_input_state = local_input;
            portEXIT_CRITICAL(&s_input_lock);
        }

        vTaskDelay(DIAG_INPUT_POLL_TICKS);
    }
}

static void display_task(void *arg)
{
    (void)arg;
    int64_t last_probe_ms = -3000;
    int64_t last_status_ms = 0;
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (!s_display_ready && now_ms - last_probe_ms >= 3000) {
            s_display_ready = diag_oled_init(&s_oled_address);
            last_probe_ms = now_ms;
        }

        if (s_display_ready) {
            oled_render_runtime();
        }

        if (s_display_ready && now_ms - last_status_ms >= 5000) {
            ESP_LOGI(TAG, "OLED ready at I2C address 0x%02X", s_oled_address);
            last_status_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void diagnostics_start(void)
{
    diag_input_init(&s_input_state);
    xTaskCreate(input_task, "diag_input", 2048, NULL, 3, NULL);
    xTaskCreate(display_task, "diag_display", 4096, NULL, 4, NULL);
}

void diagnostics_set_mode(diag_mode_t mode)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.mode = mode;
    portEXIT_CRITICAL(&s_state_lock);
}

void diagnostics_set_wifi_connected(bool connected)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.wifi_connected = connected;
    portEXIT_CRITICAL(&s_state_lock);
}

void diagnostics_add_usb_to_wifi(uint16_t len)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.usb_to_wifi_bytes += len;
    s_state.usb_to_wifi_packets++;
    portEXIT_CRITICAL(&s_state_lock);
}

void diagnostics_add_wifi_to_usb(uint16_t len)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.wifi_to_usb_bytes += len;
    s_state.wifi_to_usb_packets++;
    portEXIT_CRITICAL(&s_state_lock);
}

#else

void diagnostics_start(void) {}
void diagnostics_set_mode(diag_mode_t mode) { (void)mode; }
void diagnostics_set_wifi_connected(bool connected) { (void)connected; }
void diagnostics_add_usb_to_wifi(uint16_t len) { (void)len; }
void diagnostics_add_wifi_to_usb(uint16_t len) { (void)len; }

#endif
