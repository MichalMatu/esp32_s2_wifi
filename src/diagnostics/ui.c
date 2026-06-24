#include "diagnostics/ui.h"

#include <inttypes.h>
#include <stdio.h>

#include "diagnostics/oled.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

#define MENU_VISIBLE_ITEMS 6

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
}

static void draw_wifi(const diag_state_t *state)
{
    wifi_ap_record_t ap = {};
    wifi_config_t cfg = {};
    char line[32];
    const char *ssid = "-";

    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != 0) {
        ssid = (const char *)cfg.sta.ssid;
    }

    diag_oled_draw_text(0, 0, "WIFI");
    snprintf(line, sizeof(line), "STATE:%s", state->wifi_connected ? "CONNECTED" : "DOWN");
    diag_oled_draw_text(1, 0, line);
    snprintf(line, sizeof(line), "SSID:%.15s", ssid);
    diag_oled_draw_text(2, 0, line);

    if (state->wifi_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(line, sizeof(line), "RSSI:%d DBM", ap.rssi);
        diag_oled_draw_text(3, 0, line);
        snprintf(line, sizeof(line), "CH:%u", ap.primary);
        diag_oled_draw_text(4, 0, line);
        snprintf(line, sizeof(line), "BSSID:%02X:%02X:%02X",
                 ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        diag_oled_draw_text(5, 0, line);
    } else {
        diag_oled_draw_text(3, 0, "RSSI:-");
        diag_oled_draw_text(4, 0, "CH:-");
        diag_oled_draw_text(5, 0, "BSSID:-");
    }
}

static void draw_connect(void)
{
    diag_oled_draw_text(0, 0, "CONNECT");
    diag_oled_draw_text(1, 0, "SCAN NETWORKS");
    diag_oled_draw_text(2, 0, "PASSWORD:WEB");
    diag_oled_draw_text(3, 0, "SAVE:WEB ONLY");
    diag_oled_draw_text(4, 0, "OLED EDIT:LATER");
    diag_oled_draw_text(6, 0, "WEB:wifi.settings");
}

static void draw_bridge(const diag_state_t *state)
{
    uint8_t mac[6] = {};
    char line[32];

    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    diag_oled_draw_text(0, 0, "BRIDGE");
    snprintf(line, sizeof(line), "MODE:%s", mode_name(state->mode));
    diag_oled_draw_text(1, 0, line);
    snprintf(line, sizeof(line), "WIFI:%s", state->wifi_connected ? "LINK" : "DOWN");
    diag_oled_draw_text(2, 0, line);
    diag_oled_draw_text(3, 0, "USB:NCM");
    diag_oled_draw_text(4, 0, "IP:ROUTER DHCP");
    snprintf(line, sizeof(line), "MAC:%02X:%02X:%02X",
             mac[3], mac[4], mac[5]);
    diag_oled_draw_text(5, 0, line);
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
}

static void draw_diagnostics(const diag_state_t *state, const diag_input_state_t *input)
{
    char line[32];
    char uptime[16];

    fmt_uptime(uptime, sizeof(uptime));

    diag_oled_draw_text(0, 0, "DIAGNOSTICS");
    snprintf(line, sizeof(line), "WIFI:%s", state->wifi_connected ? "OK" : "DOWN");
    diag_oled_draw_text(1, 0, line);
    snprintf(line, sizeof(line), "UP:%s", uptime);
    diag_oled_draw_text(2, 0, line);
    snprintf(line, sizeof(line), "HEAP:%" PRIu32, esp_get_free_heap_size());
    diag_oled_draw_text(3, 0, line);
    snprintf(line, sizeof(line), "ENC:%" PRId32, input->encoder_position);
    diag_oled_draw_text(4, 0, line);
    snprintf(line, sizeof(line), "B:%" PRIu32 " OK:%" PRIu32,
             input->back_presses, input->confirm_presses);
    diag_oled_draw_text(5, 0, line);
    snprintf(line, sizeof(line), "GPIO:%d/%d/%d",
             CONFIG_DIAG_BACK_GPIO, CONFIG_DIAG_ENCODER_A_GPIO, CONFIG_DIAG_ENCODER_B_GPIO);
    diag_oled_draw_text(6, 0, line);
}

static void draw_config(void)
{
    char line[32];

    diag_oled_draw_text(0, 0, "CONFIG");
    diag_oled_draw_text(1, 0, "PORTAL:wifi.set");
    diag_oled_draw_text(2, 0, "STA IP:DHCP");
    diag_oled_draw_text(3, 0, "AP IP:CONFIG");
    diag_oled_draw_text(4, 0, "NAME:DEFAULT");
    diag_oled_draw_text(5, 0, "BRIGHT:FIXED");
    snprintf(line, sizeof(line), "OLED I2C:%d/%d",
             CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO);
    diag_oled_draw_text(6, 0, line);
}

static void draw_actions(void)
{
    diag_oled_draw_text(0, 0, "ACTIONS");
    diag_oled_draw_text(1, 0, "RECONNECT WIFI");
    diag_oled_draw_text(2, 0, "RESTART BRIDGE");
    diag_oled_draw_text(3, 0, "RESTART ESP");
    diag_oled_draw_text(4, 0, "BOOTLOADER");
    diag_oled_draw_text(5, 0, "RESET WIFI CFG");
    diag_oled_draw_text(6, 0, "FACTORY RESET");
}

static void draw_about(void)
{
    uint8_t mac[6] = {};
    char line[32];

    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    diag_oled_draw_text(0, 0, "ABOUT");
    diag_oled_draw_text(1, 0, "ESP32-S2 WIFI");
    diag_oled_draw_text(2, 0, "USB NCM BRIDGE");
    diag_oled_draw_text(3, 0, "BRANCH:OLED");
    snprintf(line, sizeof(line), "MAC:%02X:%02X:%02X",
             mac[3], mac[4], mac[5]);
    diag_oled_draw_text(4, 0, line);
    snprintf(line, sizeof(line), "SDA/SCL:%d/%d",
             CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO);
    diag_oled_draw_text(5, 0, line);
    snprintf(line, sizeof(line), "A/B:%d/%d",
             CONFIG_DIAG_ENCODER_A_GPIO, CONFIG_DIAG_ENCODER_B_GPIO);
    diag_oled_draw_text(6, 0, line);
}

static void draw_message(const char *title, const char *line1, const char *line2, const char *line3)
{
    diag_oled_draw_text(0, 0, title);
    if (line1) {
        diag_oled_draw_text(2, 0, line1);
    }
    if (line2) {
        diag_oled_draw_text(3, 0, line2);
    }
    if (line3) {
        diag_oled_draw_text(5, 0, line3);
    }
}

static void draw_scan_results(const diag_state_t *state, const char *title)
{
    char line[32];

    diag_oled_draw_text(0, 0, title);
    if (state->wifi_scan_running) {
        diag_oled_draw_text(2, 0, "SCANNING...");
        return;
    }
    if (state->wifi_scan_error != 0) {
        snprintf(line, sizeof(line), "SCAN ERR:%d", state->wifi_scan_error);
        diag_oled_draw_text(2, 0, line);
        diag_oled_draw_text(4, 0, "OK RESCAN");
        return;
    }
    if (state->wifi_scan_count == 0) {
        diag_oled_draw_text(2, 0, "NO RESULTS YET");
        diag_oled_draw_text(4, 0, "OK SCAN");
        return;
    }

    snprintf(line, sizeof(line), "FOUND:%u/%u", state->wifi_scan_count, state->wifi_scan_total);
    diag_oled_draw_text(1, 0, line);
    for (uint16_t i = 0; i < state->wifi_scan_count; i++) {
        snprintf(line, sizeof(line), "%d CH%u %.10s",
                 state->wifi_scan_results[i].rssi,
                 state->wifi_scan_results[i].channel,
                 state->wifi_scan_results[i].ssid[0] ? state->wifi_scan_results[i].ssid : "<HIDDEN>");
        diag_oled_draw_text(i + 2, 0, line);
    }
}

static void draw_status_detail(const diag_state_t *state, const diag_input_state_t *input)
{
    char line[32];
    char uptime[16];

    switch (input->detail_index) {
    case 1:
        draw_wifi(state);
        break;
    case 2:
        diag_oled_draw_text(0, 0, "USB");
        diag_oled_draw_text(1, 0, "LINK:NCM");
        snprintf(line, sizeof(line), "U2W:%" PRIu32 " PKT", state->usb_to_wifi_packets);
        diag_oled_draw_text(3, 0, line);
        snprintf(line, sizeof(line), "W2U:%" PRIu32 " PKT", state->wifi_to_usb_packets);
        diag_oled_draw_text(4, 0, line);
        break;
    case 3:
        draw_traffic(state);
        break;
    case 4:
        fmt_uptime(uptime, sizeof(uptime));
        diag_oled_draw_text(0, 0, "SYSTEM");
        snprintf(line, sizeof(line), "UP:%s", uptime);
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "HEAP:%" PRIu32, esp_get_free_heap_size());
        diag_oled_draw_text(3, 0, line);
        diag_oled_draw_text(4, 0, "USB:NCM");
        snprintf(line, sizeof(line), "I2C:%d/%d", CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO);
        diag_oled_draw_text(5, 0, line);
        break;
    default:
        draw_status(state);
        break;
    }
}

static void draw_wifi_detail(const diag_state_t *state, const diag_input_state_t *input)
{
    wifi_config_t cfg = {};
    char line[32];
    bool armed = input->action_confirm_armed;

    switch (input->detail_index) {
    case 1:
        draw_scan_results(state, "SCAN NETWORKS");
        break;
    case 2:
        diag_oled_draw_text(0, 0, "SAVED NETWORK");
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != 0) {
            snprintf(line, sizeof(line), "SSID:%.15s", (const char *)cfg.sta.ssid);
            diag_oled_draw_text(2, 0, line);
            diag_oled_draw_text(3, 0, "PASSWORD:SAVED");
        } else {
            diag_oled_draw_text(2, 0, "SSID:-");
            diag_oled_draw_text(3, 0, "PASSWORD:-");
        }
        break;
    case 3:
        draw_message("RECONNECT", "OK RUN", "NO RESTART", NULL);
        break;
    case 4:
        draw_message("FORGET NETWORK", armed ? "OK AGAIN:RESET" : "OK ARM RESET", "THEN CONFIG MODE", NULL);
        break;
    default:
        draw_wifi(state);
        break;
    }
}

static void draw_connect_detail(const diag_state_t *state, const diag_input_state_t *input)
{
    switch (input->detail_index) {
    case 0:
        draw_scan_results(state, "SELECT SSID");
        break;
    case 1:
        draw_message("ENTER PASSWORD", "OLED EDIT:LATER", "USE WIFI.SETTINGS", NULL);
        break;
    case 2:
        draw_message("SAVE+CONNECT", "WEB ONLY NOW", "wifi.settings", NULL);
        break;
    default:
        draw_message("CONNECT ONCE", "WEB ONLY NOW", "NO OLED PASS YET", NULL);
        break;
    }
}

static void draw_bridge_detail(const diag_state_t *state, const diag_input_state_t *input)
{
    switch (input->detail_index) {
    case 1:
        draw_message("USB LINK", "MODE:NCM", "MAC SPOOF:ON", "HOST:MACBOOK");
        break;
    case 2:
        draw_message("IP CONFIG", "MODE:BRIDGE", "IP FROM ROUTER", "DHCP PASSTHRU");
        break;
    case 3:
        draw_message("DNS/GATEWAY", "FROM ROUTER", "VISIBLE ON MAC", NULL);
        break;
    case 4:
        draw_message("RESTART BRIDGE",
                     input->action_confirm_armed ? "OK AGAIN:REBOOT" : "OK ARM REBOOT",
                     "USB WILL DROP",
                     NULL);
        break;
    default:
        draw_bridge(state);
        break;
    }
}

static void draw_traffic_detail(const diag_state_t *state, const diag_input_state_t *input)
{
    char line[32];

    switch (input->detail_index) {
    case 1:
        diag_oled_draw_text(0, 0, "USB TO WIFI");
        snprintf(line, sizeof(line), "BYTES:%" PRIu64 "K", state->usb_to_wifi_bytes / 1024ULL);
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "PKT:%" PRIu32, state->usb_to_wifi_packets);
        diag_oled_draw_text(3, 0, line);
        break;
    case 2:
        diag_oled_draw_text(0, 0, "WIFI TO USB");
        snprintf(line, sizeof(line), "BYTES:%" PRIu64 "K", state->wifi_to_usb_bytes / 1024ULL);
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "PKT:%" PRIu32, state->wifi_to_usb_packets);
        diag_oled_draw_text(3, 0, line);
        break;
    case 3:
        diag_oled_draw_text(0, 0, "PACKETS");
        snprintf(line, sizeof(line), "U2W:%" PRIu32, state->usb_to_wifi_packets);
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "W2U:%" PRIu32, state->wifi_to_usb_packets);
        diag_oled_draw_text(3, 0, line);
        break;
    default:
        draw_traffic(state);
        break;
    }
}

static void draw_diagnostics_detail(const diag_state_t *state, const diag_input_state_t *input)
{
    wifi_ap_record_t ap = {};
    char line[32];
    char uptime[16];

    switch (input->detail_index) {
    case 0:
        diag_oled_draw_text(0, 0, "SIGNAL/RSSI");
        if (state->wifi_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snprintf(line, sizeof(line), "RSSI:%d DBM", ap.rssi);
            diag_oled_draw_text(2, 0, line);
        } else {
            diag_oled_draw_text(2, 0, "RSSI:-");
        }
        break;
    case 1:
        diag_oled_draw_text(0, 0, "CHANNEL");
        if (state->wifi_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snprintf(line, sizeof(line), "CH:%u", ap.primary);
            diag_oled_draw_text(2, 0, line);
        } else {
            diag_oled_draw_text(2, 0, "CH:-");
        }
        break;
    case 2:
        diag_oled_draw_text(0, 0, "HEAP/RAM");
        snprintf(line, sizeof(line), "FREE:%" PRIu32, esp_get_free_heap_size());
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "MIN:%" PRIu32, esp_get_minimum_free_heap_size());
        diag_oled_draw_text(3, 0, line);
        break;
    case 3:
        fmt_uptime(uptime, sizeof(uptime));
        diag_oled_draw_text(0, 0, "UPTIME");
        snprintf(line, sizeof(line), "UP:%s", uptime);
        diag_oled_draw_text(2, 0, line);
        break;
    case 4:
        draw_message("LAST ERROR", "LOG:SERIAL NOW", "OLED BUFFER:LATER", NULL);
        break;
    case 5:
        diag_oled_draw_text(0, 0, "INPUT TEST");
        snprintf(line, sizeof(line), "ENC:%" PRId32, input->encoder_position);
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "B:%" PRIu32 " OK:%" PRIu32,
                 input->back_presses, input->confirm_presses);
        diag_oled_draw_text(3, 0, line);
        snprintf(line, sizeof(line), "A:%d B:%d", input->encoder_a_level, input->encoder_b_level);
        diag_oled_draw_text(4, 0, line);
        break;
    case 6:
        draw_message("OLED TEST", "I2C:400KHZ", "PAGE TX:128B", "DISPLAY:OK");
        break;
    default:
        draw_diagnostics(state, input);
        break;
    }
}

static void draw_config_detail(const diag_input_state_t *input)
{
    switch (input->detail_index) {
    case 0:
        draw_message("CONFIG PORTAL",
                     input->action_confirm_armed ? "OK AGAIN:RESET" : "OK ARM RESET",
                     "THEN WIFI.SET",
                     NULL);
        break;
    case 1:
        draw_message("STA IP MODE", "CURRENT:DHCP", "STATIC:LATER", NULL);
        break;
    case 2:
        draw_message("AP IP MODE", "CONFIG PORTAL", "USB LOCAL ONLY", NULL);
        break;
    case 3:
        draw_message("DEVICE NAME", "USB:ESPRESSIF", "CUSTOM:LATER", NULL);
        break;
    case 4:
        draw_message("OLED BRIGHT", "FIXED CONTRAST", "DIMMER:LATER", NULL);
        break;
    default:
        draw_message("SCREEN TIMEOUT", "ALWAYS ON NOW", "TIMEOUT:LATER", NULL);
        break;
    }
}

static void draw_actions_detail(const diag_input_state_t *input)
{
    bool armed = input->action_confirm_armed;

    switch (input->detail_index) {
    case 0:
        draw_message("RECONNECT WIFI", "OK RUN", "NO RESTART", NULL);
        break;
    case 1:
        draw_message("RESTART BRIDGE", armed ? "OK AGAIN:REBOOT" : "OK ARM REBOOT", "USB WILL DROP", NULL);
        break;
    case 2:
        draw_message("RESTART ESP", armed ? "OK AGAIN:REBOOT" : "OK ARM ACTION", "B CANCEL", NULL);
        break;
    case 3:
        draw_message("BOOTLOADER", armed ? "OK AGAIN:USB DL" : "OK ARM ACTION", "UPLOAD WITHOUT BOOT", NULL);
        break;
    case 4:
        draw_message("RESET WIFI CFG", armed ? "OK AGAIN:RESET" : "OK ARM RESET", "THEN CONFIG MODE", NULL);
        break;
    default:
        draw_message("FACTORY RESET", armed ? "OK AGAIN:ERASE" : "OK ARM ERASE", "ERASE NVS", NULL);
        break;
    }
}

static void draw_about_detail(const diag_input_state_t *input)
{
    uint8_t mac[6] = {};
    char line[32];

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    switch (input->detail_index) {
    case 0:
        draw_message("FIRMWARE", "ESP32-S2 WIFI", "USB NCM BRIDGE", "OLED MENU");
        break;
    case 1:
        draw_message("BUILD", "BRANCH:OLED", "FRAMEWORK:ESP-IDF", NULL);
        break;
    case 2:
        diag_oled_draw_text(0, 0, "MAC ADDRESS");
        snprintf(line, sizeof(line), "%02X:%02X:%02X",
                 mac[0], mac[1], mac[2]);
        diag_oled_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "%02X:%02X:%02X",
                 mac[3], mac[4], mac[5]);
        diag_oled_draw_text(3, 0, line);
        break;
    default:
        draw_about();
        break;
    }
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
    snprintf(line, sizeof(line), "SCR:%d", input->screen);
    diag_oled_draw_text(6, 0, line);
    diag_oled_draw_text(7, 0, "ROT PRESS TEST");
}

static int menu_group_item_count(diag_menu_group_t group)
{
    switch (group) {
    case DIAG_MENU_STATUS:
        return 5;
    case DIAG_MENU_WIFI:
        return 5;
    case DIAG_MENU_CONNECT:
        return 4;
    case DIAG_MENU_BRIDGE:
        return 5;
    case DIAG_MENU_TRAFFIC:
        return 4;
    case DIAG_MENU_DIAGNOSTICS:
        return 7;
    case DIAG_MENU_CONFIG:
        return 6;
    case DIAG_MENU_ACTIONS:
        return 6;
    case DIAG_MENU_ABOUT:
        return 4;
    default:
        return 1;
    }
}

static const char *menu_group_name(diag_menu_group_t group)
{
    switch (group) {
    case DIAG_MENU_STATUS:
        return "STATUS";
    case DIAG_MENU_WIFI:
        return "WIFI";
    case DIAG_MENU_CONNECT:
        return "CONNECT";
    case DIAG_MENU_BRIDGE:
        return "BRIDGE";
    case DIAG_MENU_TRAFFIC:
        return "TRAFFIC";
    case DIAG_MENU_DIAGNOSTICS:
        return "DIAGNOSTICS";
    case DIAG_MENU_CONFIG:
        return "CONFIG";
    case DIAG_MENU_ACTIONS:
        return "ACTIONS";
    case DIAG_MENU_ABOUT:
        return "ABOUT";
    default:
        return "?";
    }
}

static const char *submenu_item_name(diag_menu_group_t group, int index)
{
    switch (group) {
    case DIAG_MENU_STATUS:
        switch (index) {
        case 0: return "OVERVIEW";
        case 1: return "WIFI";
        case 2: return "USB";
        case 3: return "TRAFFIC";
        case 4: return "SYSTEM";
        default: return "?";
        }
    case DIAG_MENU_WIFI:
        switch (index) {
        case 0: return "CURRENT NETWORK";
        case 1: return "SCAN NETWORKS";
        case 2: return "SAVED NETWORK";
        case 3: return "RECONNECT";
        case 4: return "FORGET NETWORK";
        default: return "?";
        }
    case DIAG_MENU_CONNECT:
        switch (index) {
        case 0: return "SELECT SSID";
        case 1: return "ENTER PASSWORD";
        case 2: return "SAVE+CONNECT";
        case 3: return "CONNECT ONCE";
        default: return "?";
        }
    case DIAG_MENU_BRIDGE:
        switch (index) {
        case 0: return "BRIDGE STATUS";
        case 1: return "USB LINK";
        case 2: return "IP CONFIG";
        case 3: return "DNS/GATEWAY";
        case 4: return "RESTART BRIDGE";
        default: return "?";
        }
    case DIAG_MENU_TRAFFIC:
        switch (index) {
        case 0: return "LIVE COUNTERS";
        case 1: return "USB TO WIFI";
        case 2: return "WIFI TO USB";
        case 3: return "PACKETS";
        default: return "?";
        }
    case DIAG_MENU_DIAGNOSTICS:
        switch (index) {
        case 0: return "SIGNAL/RSSI";
        case 1: return "CHANNEL";
        case 2: return "HEAP/RAM";
        case 3: return "UPTIME";
        case 4: return "LAST ERROR";
        case 5: return "INPUT TEST";
        case 6: return "OLED TEST";
        default: return "?";
        }
    case DIAG_MENU_CONFIG:
        switch (index) {
        case 0: return "CONFIG PORTAL";
        case 1: return "STA IP MODE";
        case 2: return "AP IP MODE";
        case 3: return "DEVICE NAME";
        case 4: return "OLED BRIGHT";
        case 5: return "SCREEN TIMEOUT";
        default: return "?";
        }
    case DIAG_MENU_ACTIONS:
        switch (index) {
        case 0: return "RECONNECT WIFI";
        case 1: return "RESTART BRIDGE";
        case 2: return "RESTART ESP";
        case 3: return "BOOTLOADER";
        case 4: return "RESET WIFI CFG";
        case 5: return "FACTORY RESET";
        default: return "?";
        }
    case DIAG_MENU_ABOUT:
        switch (index) {
        case 0: return "FIRMWARE";
        case 1: return "BUILD";
        case 2: return "MAC ADDRESS";
        case 3: return "GPIO PINOUT";
        default: return "?";
        }
    default:
        return "?";
    }
}

static void draw_menu(const diag_input_state_t *input)
{
    int item_count = input->menu_level == DIAG_MENU_ROOT ?
                     DIAG_MENU_GROUP_COUNT :
                     menu_group_item_count(input->menu_group);
    int first = input->menu_index - MENU_VISIBLE_ITEMS + 1;

    if (first < 0) {
        first = 0;
    }
    if (first > item_count - MENU_VISIBLE_ITEMS) {
        first = item_count - MENU_VISIBLE_ITEMS;
    }
    if (first < 0) {
        first = 0;
    }

    diag_oled_draw_text(0, 0, input->menu_level == DIAG_MENU_ROOT ?
                              "MENU" :
                              menu_group_name(input->menu_group));

    for (int row = 0; row < MENU_VISIBLE_ITEMS; row++) {
        int index = first + row;
        char line[24];

        if (index >= item_count) {
            break;
        }

        const char *name = input->menu_level == DIAG_MENU_ROOT ?
                           menu_group_name((diag_menu_group_t)index) :
                           submenu_item_name(input->menu_group, index);
        snprintf(line, sizeof(line), "%c %.18s", input->menu_index == index ? '>' : ' ', name);
        diag_oled_draw_text(row + 1, 0, line);
    }

    diag_oled_draw_text(7, 0, input->menu_level == DIAG_MENU_ROOT ?
                              "OK ENTER B CLOSE" :
                              "OK OPEN  B BACK");
}

bool diag_ui_render(const diag_state_t *state, const diag_input_state_t *input)
{
    diag_oled_clear();
#if CONFIG_APP_OLED_DEBUG_ONLY
    draw_debug_status(input);
#else
    if (input->menu_open) {
        draw_menu(input);
        return diag_oled_flush();
    }

    if (input->screen_from_menu) {
        switch (input->screen) {
        case DIAG_SCREEN_WIFI:
            draw_wifi_detail(state, input);
            break;
        case DIAG_SCREEN_CONNECT:
            draw_connect_detail(state, input);
            break;
        case DIAG_SCREEN_BRIDGE:
            draw_bridge_detail(state, input);
            break;
        case DIAG_SCREEN_TRAFFIC:
            draw_traffic_detail(state, input);
            break;
        case DIAG_SCREEN_DIAGNOSTICS:
            draw_diagnostics_detail(state, input);
            break;
        case DIAG_SCREEN_CONFIG:
            draw_config_detail(input);
            break;
        case DIAG_SCREEN_ACTIONS:
            draw_actions_detail(input);
            break;
        case DIAG_SCREEN_ABOUT:
            draw_about_detail(input);
            break;
        default:
            draw_status_detail(state, input);
            break;
        }
        diag_oled_draw_text(7, 0, input->action_confirm_armed ? "OK RUN  B CANCEL" : "B BACK");
        return diag_oled_flush();
    }

    switch (input->screen) {
    case DIAG_SCREEN_WIFI:
        draw_wifi(state);
        break;
    case DIAG_SCREEN_CONNECT:
        draw_connect();
        break;
    case DIAG_SCREEN_BRIDGE:
        draw_bridge(state);
        break;
    case DIAG_SCREEN_TRAFFIC:
        draw_traffic(state);
        break;
    case DIAG_SCREEN_DIAGNOSTICS:
        draw_diagnostics(state, input);
        break;
    case DIAG_SCREEN_CONFIG:
        draw_config();
        break;
    case DIAG_SCREEN_ACTIONS:
        draw_actions();
        break;
    case DIAG_SCREEN_ABOUT:
        draw_about();
        break;
    default:
        draw_status(state);
        break;
    }
#endif
    return diag_oled_flush();
}

#endif
