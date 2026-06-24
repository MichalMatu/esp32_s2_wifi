#include "diagnostics/ui.h"

#include <inttypes.h>
#include <stdio.h>

#include "diagnostics/oled.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

static void fmt_uptime(char *buf, size_t len)
{
    uint64_t seconds = esp_timer_get_time() / 1000000ULL;
    unsigned hours = seconds / 3600;
    unsigned minutes = (seconds / 60) % 60;
    unsigned secs = seconds % 60;
    snprintf(buf, len, "%02u:%02u:%02u", hours, minutes, secs);
}

static const char *mode_name(diag_mode_t mode)
{
    switch (mode) {
    case DIAG_MODE_CONFIG:
        return "CONFIG";
    case DIAG_MODE_BRIDGE:
        return "BRIDGE";
    default:
        return "BOOT";
    }
}

static void draw_status(const diag_state_t *state)
{
    wifi_ap_record_t ap = {};
    wifi_config_t cfg = {};
    char line[32];
    const char *ssid = "-";
    int rssi = 0;
    uint8_t channel = 0;

    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != 0) {
        ssid = (const char *)cfg.sta.ssid;
    }
    if (state->wifi_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
        channel = ap.primary;
    }

    diag_oled_draw_text(0, 0, "ESP32-S2 USB");
    snprintf(line, sizeof(line), "MODE:%s", mode_name(state->mode));
    diag_oled_draw_text(1, 0, line);
    snprintf(line, sizeof(line), "WIFI:%s", state->wifi_connected ? "OK" : "DOWN");
    diag_oled_draw_text(2, 0, line);
    snprintf(line, sizeof(line), "RSSI:%d DBM", rssi);
    diag_oled_draw_text(3, 0, state->wifi_connected ? line : "RSSI:-");
    snprintf(line, sizeof(line), "CH:%u", channel);
    diag_oled_draw_text(4, 0, state->wifi_connected ? line : "CH:-");
    snprintf(line, sizeof(line), "SSID:%.15s", ssid);
    diag_oled_draw_text(5, 0, line);
    diag_oled_draw_text(7, 0, "<B  OK>");
}

static void draw_traffic(const diag_state_t *state)
{
    char line[32];

    diag_oled_draw_text(0, 0, "TRAFFIC");
    snprintf(line, sizeof(line), "USB>WIFI:%" PRIu64 "K", state->usb_to_wifi_bytes / 1024ULL);
    diag_oled_draw_text(2, 0, line);
    snprintf(line, sizeof(line), "WIFI>USB:%" PRIu64 "K", state->wifi_to_usb_bytes / 1024ULL);
    diag_oled_draw_text(3, 0, line);
    snprintf(line, sizeof(line), "U2W PKT:%" PRIu32, state->usb_to_wifi_packets);
    diag_oled_draw_text(5, 0, line);
    snprintf(line, sizeof(line), "W2U PKT:%" PRIu32, state->wifi_to_usb_packets);
    diag_oled_draw_text(6, 0, line);
    diag_oled_draw_text(7, 0, "<B  OK>");
}

static void draw_system(const diag_state_t *state)
{
    char line[32];
    char uptime[16];

    (void)state;
    fmt_uptime(uptime, sizeof(uptime));

    diag_oled_draw_text(0, 0, "SYSTEM");
    snprintf(line, sizeof(line), "UP:%s", uptime);
    diag_oled_draw_text(2, 0, line);
    snprintf(line, sizeof(line), "HEAP:%" PRIu32, esp_get_free_heap_size());
    diag_oled_draw_text(3, 0, line);
    diag_oled_draw_text(4, 0, "USB:NCM");
    snprintf(line, sizeof(line), "I2C:%d/%d", CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO);
    diag_oled_draw_text(5, 0, line);
    diag_oled_draw_text(7, 0, "<B  OK>");
}

static void draw_debug_status(const diag_input_state_t *input)
{
    char line[32];

    diag_oled_draw_text(0, 0, "OLED OK");
    snprintf(line, sizeof(line), "ENC:%" PRId32, input->encoder_position);
    diag_oled_draw_text(1, 0, line);
    snprintf(line, sizeof(line), "BACK:%" PRIu32, input->back_presses);
    diag_oled_draw_text(2, 0, line);
    snprintf(line, sizeof(line), "OK:%" PRIu32, input->confirm_presses);
    diag_oled_draw_text(3, 0, line);
    snprintf(line, sizeof(line), "A:%d B:%d", input->encoder_a_level, input->encoder_b_level);
    diag_oled_draw_text(4, 0, line);
    snprintf(line, sizeof(line), "BK:%d PSH:%d", input->back_level, input->confirm_level);
    diag_oled_draw_text(5, 0, line);
    snprintf(line, sizeof(line), "PAGE:%d", input->page);
    diag_oled_draw_text(6, 0, line);
    diag_oled_draw_text(7, 0, "ROT PRESS TEST");
}

bool diag_ui_render(const diag_state_t *state, const diag_input_state_t *input)
{
    diag_oled_clear();
#if CONFIG_APP_OLED_DEBUG_ONLY
    draw_debug_status(input);
#else
    switch (input->page) {
    case 1:
        draw_traffic(state);
        break;
    case 2:
        draw_system(state);
        break;
    default:
        draw_status(state);
        break;
    }
#endif
    return diag_oled_flush();
}

#endif
