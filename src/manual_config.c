/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_core_dump.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_private/esp_clk.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "mdns.h"

#include "config_access.h"
#include "dns_server.h"
#include "provisioning.h"
#include "status_led.h"
#include "web_assets.h"
#include "wifi_profiles.h"

static const char *TAG = "NCM_configuration";
#define FORM_BODY_MAX 512
#define CONFIG_MDNS_HOSTNAME "wifi"
#define CONFIG_MDNS_HOST_FQDN CONFIG_MDNS_HOSTNAME ".local"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_CONNECT_POLL_MS 250

typedef enum {
    WIFI_CONNECT_STATE_IDLE,
    WIFI_CONNECT_STATE_RUNNING,
    WIFI_CONNECT_STATE_SUCCEEDED,
    WIFI_CONNECT_STATE_FAILED,
} wifi_connect_state_t;

typedef enum {
    WIFI_CONNECT_ACTION_TEST,
    WIFI_CONNECT_ACTION_RECONNECT,
} wifi_connect_action_t;

typedef struct {
    bool enabled;
    bool present;
    size_t address;
    size_t size;
    char size_text[24];
    char error[48];
} core_dump_info_t;

typedef struct {
    uint32_t seq;
    wifi_connect_state_t state;
    wifi_connect_action_t action;
    bool got_ip;
    bool bridge_pending;
    int64_t started_us;
    char ssid[33];
    char password[65];
    char message[96];
    char reason[80];
    char ip[16];
    int rssi;
    uint8_t channel;
    uint8_t disconnect_reason;
} wifi_connect_status_t;

static httpd_handle_t s_web_server = NULL;
static bool s_wifi_started = false;
static bool s_scan_event_registered = false;
static bool s_wifi_connect_event_registered = false;
static bool s_mdns_started = false;
static config_access_mode_t s_active_access_mode = CONFIG_ACCESS_MODE_LOCAL_ONLY;
static EventGroupHandle_t s_provisioning_flags = NULL;
static int s_provisioning_success_bit = 0;
static SemaphoreHandle_t s_scan_mutex = NULL;
static SemaphoreHandle_t s_connect_mutex = NULL;
static uint32_t s_scan_seq = 0;
static provisioning_scan_state_t s_scan_state = PROVISIONING_SCAN_STATE_IDLE;
static EXT_RAM_BSS_ATTR wifi_ap_record_t s_scan_records[PROVISIONING_SCAN_MAX_RESULTS];
static uint16_t s_scan_total = 0;
static uint16_t s_scan_count = 0;
static uint32_t s_scan_duration_ms = 0;
static int64_t s_scan_started_us = 0;
static char s_scan_error[80] = {};
static wifi_ap_record_t *s_scan_snapshot_records = NULL;
static wifi_connect_status_t s_connect_status = {
    .state = WIFI_CONNECT_STATE_IDLE,
    .message = "No Wi-Fi connection test has run yet.",
};

static void scan_done_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                              void *event_data);
static void wifi_connect_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                       void *event_data);
static void sort_scan_records(wifi_ap_record_t *records, uint16_t count);

bool is_provisioned(void) {
    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        return false;
    }

    return strlen((const char *)wifi_cfg.sta.ssid) > 0;
}

const char *provisioning_wifi_auth_mode_name(wifi_auth_mode_t auth_mode) {
    switch (auth_mode) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2 Enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI";
    default:
        return "unknown";
    }
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

const char *provisioning_scan_state_name(provisioning_scan_state_t state) {
    switch (state) {
    case PROVISIONING_SCAN_STATE_IDLE:
        return "idle";
    case PROVISIONING_SCAN_STATE_RUNNING:
        return "running";
    case PROVISIONING_SCAN_STATE_DONE:
        return "done";
    case PROVISIONING_SCAN_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static const char *wifi_connect_state_name(wifi_connect_state_t state) {
    switch (state) {
    case WIFI_CONNECT_STATE_IDLE:
        return "idle";
    case WIFI_CONNECT_STATE_RUNNING:
        return "running";
    case WIFI_CONNECT_STATE_SUCCEEDED:
        return "succeeded";
    case WIFI_CONNECT_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static const char *wifi_disconnect_reason_name(uint8_t reason) {
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "network not found";
    case WIFI_REASON_AUTH_FAIL:
        return "authentication failed";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "WPA handshake timed out";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection failed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-way handshake timed out";
    case WIFI_REASON_ASSOC_LEAVE:
        return "station left AP";
    default:
        return "disconnected";
    }
}

static void format_bytes(uint32_t bytes, char *out, size_t out_len) {
    if (bytes >= 1024 * 1024) {
        snprintf(out, out_len, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, out_len, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%" PRIu32 " B", bytes);
    }
}

static void format_uptime(char *out, size_t out_len) {
    uint64_t seconds = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t days = seconds / 86400ULL;
    seconds %= 86400ULL;
    uint32_t hours = seconds / 3600ULL;
    seconds %= 3600ULL;
    uint32_t minutes = seconds / 60ULL;
    seconds %= 60ULL;

    if (days) {
        snprintf(out, out_len, "%" PRIu32 "d %" PRIu32 "h %" PRIu32 "m", days, hours, minutes);
    } else if (hours) {
        snprintf(out, out_len, "%" PRIu32 "h %" PRIu32 "m %" PRIu64 "s", hours, minutes, seconds);
    } else {
        snprintf(out, out_len, "%" PRIu32 "m %" PRIu64 "s", minutes, seconds);
    }
}

static esp_err_t ensure_wifi_started(void) {
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Cannot set Wi-Fi STA mode");

    if (!s_scan_event_registered) {
        ESP_RETURN_ON_ERROR(
            esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, scan_done_handler, NULL),
            TAG, "Cannot register Wi-Fi scan handler");
        s_scan_event_registered = true;
    }

    if (!s_wifi_connect_event_registered) {
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                                       wifi_connect_event_handler, NULL),
                            TAG, "Cannot register Wi-Fi connected handler");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                       wifi_connect_event_handler, NULL),
                            TAG, "Cannot register Wi-Fi disconnected handler");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       wifi_connect_event_handler, NULL),
                            TAG, "Cannot register Wi-Fi IP handler");
        s_wifi_connect_event_registered = true;
    }

    if (!s_wifi_started) {
        esp_err_t ret = esp_wifi_start();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
            return ret;
        }
        s_wifi_started = true;
    }

    return ESP_OK;
}

static esp_err_t init_connect_resources(void) {
    if (s_connect_mutex == NULL) {
        s_connect_mutex = xSemaphoreCreateMutex();
        if (s_connect_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t init_scan_resources(void) {
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
        if (s_scan_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_scan_snapshot_records == NULL) {
        s_scan_snapshot_records =
            heap_caps_calloc(PROVISIONING_SCAN_MAX_RESULTS, sizeof(s_scan_snapshot_records[0]),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_scan_snapshot_records == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static void sort_scan_records(wifi_ap_record_t *records, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        for (uint16_t j = i + 1; j < count; j++) {
            if (records[j].rssi > records[i].rssi) {
                wifi_ap_record_t tmp = records[i];
                records[i] = records[j];
                records[j] = tmp;
            }
        }
    }
}

static esp_err_t send_chunk(httpd_req_t *req, const char *text) {
    return httpd_resp_sendstr_chunk(req, text);
}

static esp_err_t send_json_string(httpd_req_t *req, const char *text) {
    ESP_RETURN_ON_ERROR(send_chunk(req, "\""), TAG, "Cannot send JSON quote");

    const char *start = text;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        const char *replacement = NULL;
        char unicode_escape[7];

        switch (*cursor) {
        case '\\':
            replacement = "\\\\";
            break;
        case '"':
            replacement = "\\\"";
            break;
        case '\b':
            replacement = "\\b";
            break;
        case '\f':
            replacement = "\\f";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            if (*cursor < 0x20) {
                snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", *cursor);
                replacement = unicode_escape;
            }
            break;
        }

        if (replacement) {
            const char *current = (const char *)cursor;
            if (current > start) {
                ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, start, current - start), TAG,
                                    "Cannot send JSON chunk");
            }
            ESP_RETURN_ON_ERROR(send_chunk(req, replacement), TAG, "Cannot send JSON escape");
            cursor++;
            start = (const char *)cursor;
        } else {
            cursor++;
        }
    }

    if ((const char *)cursor > start) {
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, start, (const char *)cursor - start), TAG,
                            "Cannot send JSON tail");
    }
    return send_chunk(req, "\"");
}

static esp_err_t send_json_field(httpd_req_t *req, const char *key, const char *value,
                                 bool trailing_comma) {
    ESP_RETURN_ON_ERROR(send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(send_chunk(req, ":"), TAG, "Cannot send JSON colon");
    ESP_RETURN_ON_ERROR(send_json_string(req, value), TAG, "Cannot send JSON value");
    if (trailing_comma) {
        return send_chunk(req, ",");
    }
    return ESP_OK;
}

static esp_err_t send_json_bool_field(httpd_req_t *req, const char *key, bool value,
                                      bool trailing_comma) {
    ESP_RETURN_ON_ERROR(send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(send_chunk(req, value ? ":true" : ":false"), TAG, "Cannot send JSON bool");
    if (trailing_comma) {
        return send_chunk(req, ",");
    }
    return ESP_OK;
}

static esp_err_t send_json_uint_field(httpd_req_t *req, const char *key, uint32_t value,
                                      bool trailing_comma) {
    char text[16];
    snprintf(text, sizeof(text), "%" PRIu32, value);
    ESP_RETURN_ON_ERROR(send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(send_chunk(req, ":"), TAG, "Cannot send JSON colon");
    ESP_RETURN_ON_ERROR(send_chunk(req, text), TAG, "Cannot send JSON uint");
    if (trailing_comma) {
        return send_chunk(req, ",");
    }
    return ESP_OK;
}

static esp_err_t send_json_nullable_int_field(httpd_req_t *req, const char *key, int value,
                                              bool has_value, bool trailing_comma) {
    ESP_RETURN_ON_ERROR(send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(send_chunk(req, ":"), TAG, "Cannot send JSON colon");
    if (has_value) {
        char text[16];
        snprintf(text, sizeof(text), "%d", value);
        ESP_RETURN_ON_ERROR(send_chunk(req, text), TAG, "Cannot send JSON int");
    } else {
        ESP_RETURN_ON_ERROR(send_chunk(req, "null"), TAG, "Cannot send JSON null");
    }
    if (trailing_comma) {
        return send_chunk(req, ",");
    }
    return ESP_OK;
}

static void prepare_json_response(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static void load_core_dump_info(core_dump_info_t *info) {
    memset(info, 0, sizeof(*info));
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    info->enabled = true;
#endif

    esp_err_t ret = esp_core_dump_image_get(&info->address, &info->size);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NOT_FOUND) {
            strlcpy(info->error, esp_err_to_name(ret), sizeof(info->error));
        }
        strlcpy(info->size_text, "0 B", sizeof(info->size_text));
        return;
    }

    info->present = info->size > 0;
    format_bytes(info->size, info->size_text, sizeof(info->size_text));
}

static esp_err_t read_form_body(httpd_req_t *req, char *out, size_t out_len) {
    if (!out || out_len == 0 || req->content_len == 0 || req->content_len >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, out + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    out[received] = '\0';
    return ESP_OK;
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool decode_form_component(const char *start, const char *end, char *out, size_t out_len) {
    if (!start || !end || !out || out_len == 0 || end < start) {
        return false;
    }

    size_t written = 0;
    for (const char *cursor = start; cursor < end; cursor++) {
        char decoded = *cursor;
        if (*cursor == '+') {
            decoded = ' ';
        } else if (*cursor == '%') {
            if (cursor + 2 >= end) {
                return false;
            }
            int high = hex_digit(cursor[1]);
            int low = hex_digit(cursor[2]);
            if (high < 0 || low < 0) {
                return false;
            }
            decoded = (char)((high << 4) | low);
            cursor += 2;
        }

        if (written + 1 >= out_len) {
            return false;
        }
        out[written++] = decoded;
    }
    out[written] = '\0';
    return true;
}

static bool get_form_value(const char *form, const char *key, char *out, size_t out_len) {
    if (!form || !key || !out || out_len == 0) {
        return false;
    }

    size_t key_len = strlen(key);
    const char *cursor = form;
    while (*cursor) {
        const char *field_end = strchr(cursor, '&');
        if (!field_end) {
            field_end = cursor + strlen(cursor);
        }

        const char *equals = memchr(cursor, '=', field_end - cursor);
        if (equals && (size_t)(equals - cursor) == key_len && strncmp(cursor, key, key_len) == 0) {
            return decode_form_component(equals + 1, field_end, out, out_len);
        }

        cursor = *field_end == '&' ? field_end + 1 : field_end;
    }
    return false;
}

static bool get_form_index(const char *form, const char *key, size_t *out) {
    char value[8] = {};
    if (!out || !get_form_value(form, key, value, sizeof(value)) || value[0] == '\0') {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (!end || *end != '\0' || parsed >= WIFI_PROFILE_MAX) {
        return false;
    }

    *out = (size_t)parsed;
    return true;
}

static esp_err_t apply_wifi_credentials(const char *ssid, const char *password,
                                        wifi_storage_t storage) {
    wifi_config_t wifi_cfg = {};

    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));

    ESP_RETURN_ON_ERROR(ensure_wifi_started(), TAG, "Cannot start Wi-Fi before saving config");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(storage), TAG, "Cannot set Wi-Fi storage");
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
}

static esp_err_t persist_wifi_credentials(const char *ssid, const char *password) {
    ESP_RETURN_ON_ERROR(apply_wifi_credentials(ssid, password, WIFI_STORAGE_FLASH), TAG,
                        "Cannot persist Wi-Fi config");
    return wifi_profiles_save(ssid, password);
}

static void import_current_wifi_config_as_profile(void) {
    wifi_config_t wifi_cfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK || wifi_cfg.sta.ssid[0] == '\0') {
        return;
    }

    wifi_profile_t profiles[WIFI_PROFILE_MAX];
    size_t profile_count = 0;
    if (wifi_profiles_load(profiles, WIFI_PROFILE_MAX, &profile_count) != ESP_OK) {
        return;
    }

    const char *ssid = (const char *)wifi_cfg.sta.ssid;
    for (size_t i = 0; i < profile_count; i++) {
        if (strcmp(profiles[i].ssid, ssid) == 0) {
            return;
        }
    }

    esp_err_t ret = wifi_profiles_save(ssid, (const char *)wifi_cfg.sta.password);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Cannot import current Wi-Fi config as profile: %s", esp_err_to_name(ret));
    }
}

static void copy_connect_status(wifi_connect_status_t *out) {
    if (!out) {
        return;
    }
    if (s_connect_mutex && xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
        *out = s_connect_status;
        xSemaphoreGive(s_connect_mutex);
    } else {
        memset(out, 0, sizeof(*out));
        out->state = WIFI_CONNECT_STATE_IDLE;
        strlcpy(out->message, "Wi-Fi connection status is not available.", sizeof(out->message));
    }
}

static bool connect_status_is_active(uint32_t seq, wifi_connect_status_t *snapshot) {
    bool active = false;
    if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_connect_status.seq == seq && s_connect_status.state == WIFI_CONNECT_STATE_RUNNING) {
            active = true;
            if (snapshot) {
                *snapshot = s_connect_status;
            }
        }
        xSemaphoreGive(s_connect_mutex);
    }
    return active;
}

static bool connect_validation_is_running(void) {
    bool running = false;
    if (s_connect_mutex && xSemaphoreTake(s_connect_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        running = s_connect_status.state == WIFI_CONNECT_STATE_RUNNING;
        xSemaphoreGive(s_connect_mutex);
    }
    return running;
}

static bool scan_is_running(void) {
    bool running = false;
    if (s_scan_mutex && xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        running = s_scan_state == PROVISIONING_SCAN_STATE_RUNNING;
        xSemaphoreGive(s_scan_mutex);
    }
    return running;
}

static void mark_connect_rejected(const char *message, const char *reason) {
    if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    uint32_t seq = s_connect_status.seq + 1;
    memset(&s_connect_status, 0, sizeof(s_connect_status));
    s_connect_status.seq = seq;
    s_connect_status.state = WIFI_CONNECT_STATE_FAILED;
    s_connect_status.started_us = esp_timer_get_time();
    strlcpy(s_connect_status.message, message, sizeof(s_connect_status.message));
    strlcpy(s_connect_status.reason, reason, sizeof(s_connect_status.reason));
    xSemaphoreGive(s_connect_mutex);
}

static void mark_connect_failed(uint32_t seq, const char *message, const char *reason,
                                uint8_t disconnect_reason) {
    if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_connect_status.seq == seq && s_connect_status.state == WIFI_CONNECT_STATE_RUNNING) {
        s_connect_status.state = WIFI_CONNECT_STATE_FAILED;
        s_connect_status.got_ip = false;
        s_connect_status.bridge_pending = false;
        s_connect_status.disconnect_reason = disconnect_reason;
        strlcpy(s_connect_status.message, message, sizeof(s_connect_status.message));
        strlcpy(s_connect_status.reason, reason, sizeof(s_connect_status.reason));
        status_led_set_state(STATUS_LED_STATE_DISCONNECTED);
    }
    xSemaphoreGive(s_connect_mutex);
}

// cppcheck-suppress constParameterCallback
static void wifi_connect_validation_task(void *arg) {
    uint32_t seq = (uint32_t)(uintptr_t)arg;
    int64_t deadline_us = esp_timer_get_time() + (int64_t)WIFI_CONNECT_TIMEOUT_MS * 1000;
    wifi_connect_status_t snapshot = {};

    while (esp_timer_get_time() < deadline_us) {
        if (!connect_status_is_active(seq, &snapshot)) {
            vTaskDelete(NULL);
        }
        if (snapshot.got_ip) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_POLL_MS));
    }

    if (!snapshot.got_ip) {
        mark_connect_failed(seq, "Wi-Fi connection timed out.",
                            "No IP address was received before timeout.", 0);
        (void)esp_wifi_disconnect();
        vTaskDelete(NULL);
    }

    if (snapshot.action == WIFI_CONNECT_ACTION_RECONNECT) {
        esp_err_t ret = persist_wifi_credentials(snapshot.ssid, snapshot.password);
        if (ret != ESP_OK) {
            mark_connect_failed(seq, "Wi-Fi connected but credentials could not be saved.",
                                esp_err_to_name(ret), 0);
            vTaskDelete(NULL);
        }
    }

    if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_connect_status.seq == seq && s_connect_status.state == WIFI_CONNECT_STATE_RUNNING) {
            s_connect_status.state = WIFI_CONNECT_STATE_SUCCEEDED;
            s_connect_status.bridge_pending = snapshot.action == WIFI_CONNECT_ACTION_RECONNECT;
            if (snapshot.action == WIFI_CONNECT_ACTION_RECONNECT) {
                strlcpy(s_connect_status.message,
                        "Wi-Fi validated. Credentials saved. Restarting into bridge mode.",
                        sizeof(s_connect_status.message));
            } else {
                strlcpy(s_connect_status.message,
                        "Wi-Fi test passed. Credentials were not saved.",
                        sizeof(s_connect_status.message));
            }
            s_connect_status.reason[0] = '\0';
            status_led_set_state(STATUS_LED_STATE_CONNECTED);
        }
        xSemaphoreGive(s_connect_mutex);
    }

    if (snapshot.action == WIFI_CONNECT_ACTION_RECONNECT && s_provisioning_flags) {
        xEventGroupSetBits(s_provisioning_flags, s_provisioning_success_bit);
    }

    vTaskDelete(NULL);
}

static esp_err_t start_wifi_connection_attempt(const char *ssid, const char *password,
                                               wifi_connect_action_t action) {
    ESP_RETURN_ON_ERROR(init_connect_resources(), TAG, "Cannot allocate Wi-Fi connect state");
    if (scan_is_running()) {
        mark_connect_rejected("Wi-Fi scan is running.", "Wait for scan results before connecting.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(apply_wifi_credentials(ssid, password, WIFI_STORAGE_RAM), TAG,
                        "Cannot apply Wi-Fi credentials for validation");

    (void)esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    uint32_t seq = 0;
    if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
        seq = s_connect_status.seq + 1;
        memset(&s_connect_status, 0, sizeof(s_connect_status));
        s_connect_status.seq = seq;
        s_connect_status.state = WIFI_CONNECT_STATE_RUNNING;
        s_connect_status.action = action;
        s_connect_status.started_us = esp_timer_get_time();
        strlcpy(s_connect_status.ssid, ssid, sizeof(s_connect_status.ssid));
        strlcpy(s_connect_status.password, password, sizeof(s_connect_status.password));
        strlcpy(s_connect_status.message, "Testing Wi-Fi connection.",
                sizeof(s_connect_status.message));
        xSemaphoreGive(s_connect_mutex);
    }

    status_led_set_state(STATUS_LED_STATE_CONNECTING);
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        mark_connect_failed(seq, "Wi-Fi connect request failed.", esp_err_to_name(ret), 0);
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(wifi_connect_validation_task, "wifi_validate", 4096,
                                      (void *)(uintptr_t)seq, 5, NULL);
    if (task_ret != pdPASS) {
        mark_connect_failed(seq, "Wi-Fi validation task could not start.", "out of memory", 0);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void wifi_connect_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                       void *event_data) {
    (void)arg;

    if (!s_connect_mutex) {
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_connect_status.state == WIFI_CONNECT_STATE_RUNNING) {
                wifi_event_sta_connected_t *connected = (wifi_event_sta_connected_t *)event_data;
                s_connect_status.channel = connected ? connected->channel : 0;
                strlcpy(s_connect_status.message, "Associated with access point.",
                        sizeof(s_connect_status.message));
            }
            xSemaphoreGive(s_connect_mutex);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got_ip = (ip_event_got_ip_t *)event_data;
        wifi_ap_record_t ap = {};
        bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;

        if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_connect_status.state == WIFI_CONNECT_STATE_RUNNING) {
                s_connect_status.got_ip = true;
                if (got_ip) {
                    esp_ip4addr_ntoa(&got_ip->ip_info.ip, s_connect_status.ip,
                                     sizeof(s_connect_status.ip));
                }
                if (have_ap) {
                    s_connect_status.rssi = ap.rssi;
                    s_connect_status.channel = ap.primary;
                }
                strlcpy(s_connect_status.message, "Wi-Fi connected. Saving credentials.",
                        sizeof(s_connect_status.message));
            }
            xSemaphoreGive(s_connect_mutex);
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = disconnected ? disconnected->reason : 0;
        int64_t now_us = esp_timer_get_time();

        if (xSemaphoreTake(s_connect_mutex, portMAX_DELAY) == pdTRUE) {
            bool ignore_local_leave = s_connect_status.state == WIFI_CONNECT_STATE_RUNNING &&
                                      reason == WIFI_REASON_ASSOC_LEAVE &&
                                      now_us - s_connect_status.started_us < 1000000;
            if (s_connect_status.state == WIFI_CONNECT_STATE_RUNNING && !s_connect_status.got_ip &&
                !ignore_local_leave) {
                s_connect_status.state = WIFI_CONNECT_STATE_FAILED;
                s_connect_status.got_ip = false;
                s_connect_status.bridge_pending = false;
                s_connect_status.disconnect_reason = reason;
                strlcpy(s_connect_status.message, "Wi-Fi connection failed.",
                        sizeof(s_connect_status.message));
                snprintf(s_connect_status.reason, sizeof(s_connect_status.reason), "%s (%u)",
                         wifi_disconnect_reason_name(reason), (unsigned)reason);
                status_led_set_state(STATUS_LED_STATE_DISCONNECTED);
            }
            xSemaphoreGive(s_connect_mutex);
        }
    }
}

static esp_err_t send_connection_object(httpd_req_t *req, const wifi_connect_status_t *status) {
    uint32_t elapsed_ms = 0;
    if (status->started_us > 0) {
        elapsed_ms = (uint32_t)((esp_timer_get_time() - status->started_us) / 1000);
    }
    bool ok = status->state != WIFI_CONNECT_STATE_FAILED;
    bool has_rssi = status->got_ip || status->state == WIFI_CONNECT_STATE_SUCCEEDED;
    bool has_channel = status->channel != 0;

    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"ok\":"), TAG, "Cannot send connection JSON");
    ESP_RETURN_ON_ERROR(send_chunk(req, ok ? "true," : "false,"), TAG, "Cannot send connection ok");
    ESP_RETURN_ON_ERROR(send_json_field(req, "state", wifi_connect_state_name(status->state), true),
                        TAG, "Cannot send connection state");
    ESP_RETURN_ON_ERROR(send_json_field(req, "ssid", status->ssid, true), TAG,
                        "Cannot send connection SSID");
    ESP_RETURN_ON_ERROR(send_json_field(req, "message", status->message, true), TAG,
                        "Cannot send connection message");
    ESP_RETURN_ON_ERROR(send_json_field(req, "reason", status->reason, true), TAG,
                        "Cannot send connection reason");
    ESP_RETURN_ON_ERROR(send_json_field(req, "ip", status->ip, true), TAG,
                        "Cannot send connection IP");
    ESP_RETURN_ON_ERROR(send_json_nullable_int_field(req, "rssi", status->rssi, has_rssi, true),
                        TAG, "Cannot send connection RSSI");
    ESP_RETURN_ON_ERROR(
        send_json_nullable_int_field(req, "channel", status->channel, has_channel, true), TAG,
        "Cannot send connection channel");
    ESP_RETURN_ON_ERROR(send_json_uint_field(req, "elapsedMs", elapsed_ms, true), TAG,
                        "Cannot send connection elapsed");
    ESP_RETURN_ON_ERROR(send_json_bool_field(req, "bridgePending", status->bridge_pending, false),
                        TAG, "Cannot send connection bridge state");
    return send_chunk(req, "}");
}

static esp_err_t send_connection_response(httpd_req_t *req) {
    wifi_connect_status_t status;
    copy_connect_status(&status);

    prepare_json_response(req);
    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"source\":\"device\","), TAG,
                        "Cannot send connection response");
    ESP_RETURN_ON_ERROR(send_chunk(req, "\"connection\":"), TAG, "Cannot send connection key");
    ESP_RETURN_ON_ERROR(send_connection_object(req, &status), TAG, "Cannot send connection object");
    ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close connection response");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t send_action_json(httpd_req_t *req, bool ok, const char *message) {
    prepare_json_response(req);
    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"ok\":"), TAG, "Cannot send action JSON");
    ESP_RETURN_ON_ERROR(send_chunk(req, ok ? "true," : "false,"), TAG, "Cannot send action state");
    ESP_RETURN_ON_ERROR(send_json_field(req, "message", message, false), TAG,
                        "Cannot send action message");
    ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close action JSON");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, WEB_INDEX_HTML, WEB_INDEX_HTML_LEN);
}

static esp_err_t status_handler(httpd_req_t *req) {
    char uptime[40];
    char value[80];
    char free_heap[24];
    char min_heap[24];
    char flash_size_text[24];
    char app_size_text[24];
    uint32_t flash_size = 0;
    const esp_partition_t *running = esp_ota_get_running_partition();
    config_access_mode_t saved_access_mode = config_access_get_mode();
    status_led_mode_t led_mode = status_led_get_mode();
    status_led_state_t led_state = status_led_get_state();
    core_dump_info_t core_dump;
    wifi_connect_status_t connect_status;

    copy_connect_status(&connect_status);
    format_uptime(uptime, sizeof(uptime));
    format_bytes(esp_get_free_heap_size(), free_heap, sizeof(free_heap));
    format_bytes(esp_get_minimum_free_heap_size(), min_heap, sizeof(min_heap));
    load_core_dump_info(&core_dump);
    if (esp_flash_get_size(esp_flash_default_chip, &flash_size) == ESP_OK) {
        format_bytes(flash_size, flash_size_text, sizeof(flash_size_text));
    } else {
        strlcpy(flash_size_text, "unknown", sizeof(flash_size_text));
    }
    if (running) {
        format_bytes(running->size, app_size_text, sizeof(app_size_text));
    } else {
        strlcpy(app_size_text, "unknown", sizeof(app_size_text));
    }

    prepare_json_response(req);
    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"source\":\"device\",\"system\":{"), TAG,
                        "Cannot send status JSON");
    ESP_RETURN_ON_ERROR(send_json_field(req, "runtime", uptime, true), TAG, "Cannot send runtime");
    snprintf(value, sizeof(value), "%d MHz", esp_clk_cpu_freq() / 1000000);
    ESP_RETURN_ON_ERROR(send_json_field(req, "cpu", value, true), TAG, "Cannot send CPU");
    ESP_RETURN_ON_ERROR(send_json_field(req, "reset", reset_reason_name(esp_reset_reason()), true),
                        TAG, "Cannot send reset");
    ESP_RETURN_ON_ERROR(send_json_field(req, "idf", esp_get_idf_version(), true), TAG,
                        "Cannot send IDF");
    ESP_RETURN_ON_ERROR(send_json_field(req, "build", __DATE__ " " __TIME__, false), TAG,
                        "Cannot send build");

    ESP_RETURN_ON_ERROR(send_chunk(req, "},\"memory\":{"), TAG, "Cannot send memory JSON");
    ESP_RETURN_ON_ERROR(send_json_field(req, "freeHeap", free_heap, true), TAG,
                        "Cannot send free heap");
    ESP_RETURN_ON_ERROR(send_json_field(req, "minFreeHeap", min_heap, true), TAG,
                        "Cannot send min heap");
    ESP_RETURN_ON_ERROR(send_json_field(req, "flashChip", flash_size_text, true), TAG,
                        "Cannot send flash");
    ESP_RETURN_ON_ERROR(send_json_field(req, "appPartition", app_size_text, false), TAG,
                        "Cannot send app partition");

    ESP_RETURN_ON_ERROR(send_chunk(req, "},\"coredump\":{"), TAG, "Cannot send coredump JSON");
    ESP_RETURN_ON_ERROR(send_json_bool_field(req, "enabled", core_dump.enabled, true), TAG,
                        "Cannot send coredump enabled");
    ESP_RETURN_ON_ERROR(send_json_bool_field(req, "present", core_dump.present, true), TAG,
                        "Cannot send coredump present");
    ESP_RETURN_ON_ERROR(send_json_field(req, "size", core_dump.size_text, true), TAG,
                        "Cannot send coredump size");
    ESP_RETURN_ON_ERROR(send_json_field(req, "error", core_dump.error, false), TAG,
                        "Cannot send coredump error");

    ESP_RETURN_ON_ERROR(send_chunk(req, "},\"network\":{"), TAG, "Cannot send network JSON");
    ESP_RETURN_ON_ERROR(send_json_field(req, "mode", "configuration", true), TAG,
                        "Cannot send mode");
    ESP_RETURN_ON_ERROR(send_json_field(req, "usb", "NCM config device", true), TAG,
                        "Cannot send USB");
    ESP_RETURN_ON_ERROR(
        send_json_field(req, "configAccess", config_access_mode_label(s_active_access_mode), true),
        TAG, "Cannot send config access");
    ESP_RETURN_ON_ERROR(
        send_json_field(req, "host", config_access_mode_host(s_active_access_mode), false), TAG,
        "Cannot send host");

    ESP_RETURN_ON_ERROR(send_chunk(req, "},\"config\":{"), TAG, "Cannot send config JSON");
    ESP_RETURN_ON_ERROR(
        send_json_field(req, "activeMode", config_access_mode_name(s_active_access_mode), true),
        TAG, "Cannot send active mode");
    ESP_RETURN_ON_ERROR(
        send_json_field(req, "savedMode", config_access_mode_name(saved_access_mode), false), TAG,
        "Cannot send saved mode");

    ESP_RETURN_ON_ERROR(send_chunk(req, "},\"connection\":"), TAG, "Cannot send connection status");
    ESP_RETURN_ON_ERROR(send_connection_object(req, &connect_status), TAG,
                        "Cannot send connection status object");

    ESP_RETURN_ON_ERROR(send_chunk(req, ",\"led\":{"), TAG, "Cannot send LED JSON");
    ESP_RETURN_ON_ERROR(send_json_field(req, "mode", status_led_get_mode_name(led_mode), true), TAG,
                        "Cannot send LED mode");
    ESP_RETURN_ON_ERROR(send_json_field(req, "state", status_led_get_state_name(led_state), false),
                        TAG, "Cannot send LED state");
    ESP_RETURN_ON_ERROR(send_chunk(req, "}}"), TAG, "Cannot close status JSON");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t coredump_download_handler(httpd_req_t *req) {
    core_dump_info_t info;
    load_core_dump_info(&info);

    if (!info.present) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_action_json(req, false, "No core dump is stored in flash.");
    }

    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!partition || info.address < partition->address ||
        info.address + info.size > partition->address + partition->size) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_action_json(req, false, "Core dump partition is invalid.");
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"esp32-s2-coredump.elf\"");

    uint8_t buffer[1024];
    size_t offset = info.address - partition->address;
    size_t remaining = info.size;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        esp_err_t ret = esp_partition_read(partition, offset, buffer, chunk);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Core dump read failed at offset %u: %s", (unsigned)offset,
                     esp_err_to_name(ret));
            return ESP_FAIL;
        }
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, (const char *)buffer, chunk), TAG,
                            "Cannot send core dump chunk");
        offset += chunk;
        remaining -= chunk;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t coredump_erase_handler(httpd_req_t *req) {
    esp_err_t ret = esp_core_dump_image_erase();
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Core dump erase failed: %s", esp_err_to_name(ret));
        return send_action_json(req, false, "Core dump erase failed.");
    }
    return send_action_json(req, true, "Core dump erased.");
}

static void scan_done_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                              void *event_data) {
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Cannot collect Wi-Fi scan results: scan state is locked");
        return;
    }

    uint16_t total_count = 0;
    uint16_t shown_count = PROVISIONING_SCAN_MAX_RESULTS;
    esp_err_t count_ret = esp_wifi_scan_get_ap_num(&total_count);
    esp_err_t records_ret = esp_wifi_scan_get_ap_records(&shown_count, s_scan_records);
    if (count_ret != ESP_OK || records_ret != ESP_OK) {
        esp_err_t ret = count_ret != ESP_OK ? count_ret : records_ret;
        snprintf(s_scan_error, sizeof(s_scan_error), "%s", esp_err_to_name(ret));
        s_scan_state = PROVISIONING_SCAN_STATE_ERROR;
        s_scan_total = 0;
        s_scan_count = 0;
        ESP_LOGW(TAG, "[scan:%" PRIu32 "] result read failed: %s", s_scan_seq, s_scan_error);
    } else {
        sort_scan_records(s_scan_records, shown_count);
        s_scan_state = PROVISIONING_SCAN_STATE_DONE;
        s_scan_total = total_count;
        s_scan_count = shown_count;
        s_scan_error[0] = '\0';
    }

    s_scan_duration_ms = (uint32_t)((esp_timer_get_time() - s_scan_started_us) / 1000);
    ESP_LOGI(TAG,
             "[scan:%" PRIu32 "] %s dur_ms=%" PRIu32 " free=%u min=%u largest=%u "
             "total=%u shown=%u",
             s_scan_seq, provisioning_scan_state_name(s_scan_state), s_scan_duration_ms,
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), s_scan_total,
             s_scan_count);
    xSemaphoreGive(s_scan_mutex);
}

static esp_err_t send_scan_snapshot(httpd_req_t *req) {
    uint16_t total_count = 0;
    uint16_t shown_count = 0;
    uint32_t duration_ms = 0;
    provisioning_scan_state_t state = PROVISIONING_SCAN_STATE_IDLE;
    char error[sizeof(s_scan_error)] = {};

    if (s_scan_snapshot_records == NULL) {
        prepare_json_response(req);
        ESP_RETURN_ON_ERROR(send_chunk(req,
                                       "{\"source\":\"device\",\"ok\":false,\"state\":\"error\","
                                       "\"total\":0,\"durationMs\":0,\"networks\":[],\"error\":"),
                            TAG, "Cannot send scan no-memory response");
        ESP_RETURN_ON_ERROR(send_json_string(req, "scan snapshot memory is unavailable"), TAG,
                            "Cannot send scan no-memory message");
        ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close scan no-memory response");
        return httpd_resp_send_chunk(req, NULL, 0);
    }

    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        state = PROVISIONING_SCAN_STATE_ERROR;
        strlcpy(error, "scan state busy", sizeof(error));
    } else {
        state = s_scan_state;
        total_count = s_scan_total;
        shown_count = s_scan_count;
        duration_ms = s_scan_duration_ms;
        if (shown_count > PROVISIONING_SCAN_MAX_RESULTS) {
            shown_count = PROVISIONING_SCAN_MAX_RESULTS;
        }
        memcpy(s_scan_snapshot_records, s_scan_records,
               shown_count * sizeof(s_scan_snapshot_records[0]));
        strlcpy(error, s_scan_error, sizeof(error));
        xSemaphoreGive(s_scan_mutex);
    }

    bool ok = state != PROVISIONING_SCAN_STATE_ERROR;
    prepare_json_response(req);
    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"source\":\"device\",\"ok\":"), TAG,
                        "Cannot send scan JSON");
    ESP_RETURN_ON_ERROR(send_chunk(req, ok ? "true" : "false"), TAG, "Cannot send scan ok");
    ESP_RETURN_ON_ERROR(send_chunk(req, ",\"state\":"), TAG, "Cannot send scan state key");
    ESP_RETURN_ON_ERROR(send_json_string(req, provisioning_scan_state_name(state)), TAG,
                        "Cannot send scan state");
    ESP_RETURN_ON_ERROR(send_chunk(req, ",\"total\":"), TAG, "Cannot send scan total key");
    char number[16];
    snprintf(number, sizeof(number), "%u", total_count);
    ESP_RETURN_ON_ERROR(send_chunk(req, number), TAG, "Cannot send scan total");
    ESP_RETURN_ON_ERROR(send_chunk(req, ",\"durationMs\":"), TAG, "Cannot send scan duration key");
    snprintf(number, sizeof(number), "%" PRIu32, duration_ms);
    ESP_RETURN_ON_ERROR(send_chunk(req, number), TAG, "Cannot send scan duration");
    ESP_RETURN_ON_ERROR(send_chunk(req, ",\"networks\":["), TAG, "Cannot send network array");

    for (uint16_t i = 0; i < shown_count; i++) {
        char ssid[33];
        memcpy(ssid, s_scan_snapshot_records[i].ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        if (ssid[0] == '\0') {
            strlcpy(ssid, "<hidden>", sizeof(ssid));
        }

        if (i > 0) {
            ESP_RETURN_ON_ERROR(send_chunk(req, ","), TAG, "Cannot send network comma");
        }
        ESP_RETURN_ON_ERROR(send_chunk(req, "{\"ssid\":"), TAG, "Cannot send network object");
        ESP_RETURN_ON_ERROR(send_json_string(req, ssid), TAG, "Cannot send network SSID");
        snprintf(number, sizeof(number), "%d", s_scan_snapshot_records[i].rssi);
        ESP_RETURN_ON_ERROR(send_chunk(req, ",\"rssi\":"), TAG, "Cannot send RSSI key");
        ESP_RETURN_ON_ERROR(send_chunk(req, number), TAG, "Cannot send RSSI");
        ESP_RETURN_ON_ERROR(send_chunk(req, ",\"security\":"), TAG, "Cannot send security key");
        ESP_RETURN_ON_ERROR(send_json_string(req, provisioning_wifi_auth_mode_name(
                                                      s_scan_snapshot_records[i].authmode)),
                            TAG, "Cannot send security");
        snprintf(number, sizeof(number), "%u", s_scan_snapshot_records[i].primary);
        ESP_RETURN_ON_ERROR(send_chunk(req, ",\"channel\":"), TAG, "Cannot send channel key");
        ESP_RETURN_ON_ERROR(send_chunk(req, number), TAG, "Cannot send channel");
        ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close network object");
    }

    ESP_RETURN_ON_ERROR(send_chunk(req, "]"), TAG, "Cannot close network array");
    if (!ok) {
        ESP_RETURN_ON_ERROR(send_chunk(req, ",\"error\":"), TAG, "Cannot send scan error key");
        ESP_RETURN_ON_ERROR(send_json_string(req, error[0] ? error : "scan failed"), TAG,
                            "Cannot send scan error");
    }
    ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close scan JSON");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t provisioning_scan_get_snapshot(provisioning_scan_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    esp_err_t init_ret = init_scan_resources();
    if (init_ret != ESP_OK) {
        return init_ret;
    }

    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    snapshot->seq = s_scan_seq;
    snapshot->state = s_scan_state;
    snapshot->total = s_scan_total;
    snapshot->count = s_scan_count;
    snapshot->duration_ms = s_scan_duration_ms;
    if (snapshot->count > PROVISIONING_SCAN_MAX_RESULTS) {
        snapshot->count = PROVISIONING_SCAN_MAX_RESULTS;
    }
    memcpy(snapshot->records, s_scan_records, snapshot->count * sizeof(snapshot->records[0]));
    strlcpy(snapshot->error, s_scan_error, sizeof(snapshot->error));
    xSemaphoreGive(s_scan_mutex);

    return ESP_OK;
}

static esp_err_t scan_status_handler(httpd_req_t *req) {
    return send_scan_snapshot(req);
}

esp_err_t provisioning_scan_start(void) {
    esp_err_t init_ret = init_scan_resources();
    if (init_ret != ESP_OK) {
        return init_ret;
    }

    if (connect_validation_is_running()) {
        if (s_scan_mutex && xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_scan_state = PROVISIONING_SCAN_STATE_ERROR;
            strlcpy(s_scan_error, "Wi-Fi validation is running", sizeof(s_scan_error));
            s_scan_total = 0;
            s_scan_count = 0;
            s_scan_duration_ms = 0;
            xSemaphoreGive(s_scan_mutex);
        }
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        if (s_scan_mutex && xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_scan_state = PROVISIONING_SCAN_STATE_ERROR;
            snprintf(s_scan_error, sizeof(s_scan_error), "%s", esp_err_to_name(ret));
            s_scan_total = 0;
            s_scan_count = 0;
            xSemaphoreGive(s_scan_mutex);
        }
        return ret;
    }

    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_scan_state == PROVISIONING_SCAN_STATE_RUNNING) {
        xSemaphoreGive(s_scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t scan_id = ++s_scan_seq;
    s_scan_state = PROVISIONING_SCAN_STATE_RUNNING;
    s_scan_total = 0;
    s_scan_count = 0;
    s_scan_duration_ms = 0;
    s_scan_started_us = esp_timer_get_time();
    s_scan_error[0] = '\0';
    memset(s_scan_records, 0, sizeof(s_scan_records));
    ESP_LOGI(TAG, "[scan:%" PRIu32 "] start free=%u min=%u largest=%u", scan_id,
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    xSemaphoreGive(s_scan_mutex);

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active =
            {
                .min = 80,
                .max = 160,
            },
    };

    ret = esp_wifi_scan_start(&scan_config, false);
    if (ret == ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "Wi-Fi scan cannot start while Wi-Fi is busy");
    }

    if (ret != ESP_OK && s_scan_mutex &&
        xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_scan_state = PROVISIONING_SCAN_STATE_ERROR;
        if (ret == ESP_ERR_WIFI_STATE) {
            strlcpy(s_scan_error, "Wi-Fi is busy", sizeof(s_scan_error));
        } else {
            snprintf(s_scan_error, sizeof(s_scan_error), "%s", esp_err_to_name(ret));
        }
        s_scan_total = 0;
        s_scan_count = 0;
        s_scan_duration_ms = (uint32_t)((esp_timer_get_time() - s_scan_started_us) / 1000);
        ESP_LOGW(TAG, "[scan:%" PRIu32 "] start failed: %s", scan_id, s_scan_error);
        xSemaphoreGive(s_scan_mutex);
    }

    return ret;
}

static esp_err_t scan_start_handler(httpd_req_t *req) {
    (void)provisioning_scan_start();
    return send_scan_snapshot(req);
}

static esp_err_t profiles_error_json(httpd_req_t *req, const char *message) {
    prepare_json_response(req);
    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"source\":\"device\",\"ok\":false,\"profiles\":[],"
                                        "\"error\":"),
                        TAG, "Cannot send profiles error");
    ESP_RETURN_ON_ERROR(send_json_string(req, message), TAG, "Cannot send profiles error message");
    ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close profiles error");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t profiles_handler(httpd_req_t *req) {
    import_current_wifi_config_as_profile();

    wifi_profile_t profiles[WIFI_PROFILE_MAX];
    size_t profile_count = 0;
    esp_err_t ret = wifi_profiles_load(profiles, WIFI_PROFILE_MAX, &profile_count);
    if (ret != ESP_OK) {
        return profiles_error_json(req, esp_err_to_name(ret));
    }

    prepare_json_response(req);
    ESP_RETURN_ON_ERROR(send_chunk(req, "{\"source\":\"device\",\"ok\":true,\"profiles\":["), TAG,
                        "Cannot send profiles JSON");
    for (size_t i = 0; i < profile_count; i++) {
        char id[8];
        if (i > 0) {
            ESP_RETURN_ON_ERROR(send_chunk(req, ","), TAG, "Cannot send profile comma");
        }
        ESP_RETURN_ON_ERROR(send_chunk(req, "{\"id\":"), TAG, "Cannot send profile object");
        snprintf(id, sizeof(id), "%u", (unsigned)i);
        ESP_RETURN_ON_ERROR(send_chunk(req, id), TAG, "Cannot send profile id");
        ESP_RETURN_ON_ERROR(send_chunk(req, ",\"ssid\":"), TAG, "Cannot send profile SSID key");
        ESP_RETURN_ON_ERROR(send_json_string(req, profiles[i].ssid), TAG,
                            "Cannot send profile SSID");
        ESP_RETURN_ON_ERROR(send_chunk(req, ",\"password\":"), TAG,
                            "Cannot send profile password key");
        ESP_RETURN_ON_ERROR(send_json_string(req, profiles[i].password), TAG,
                            "Cannot send profile password");
        ESP_RETURN_ON_ERROR(send_chunk(req, "}"), TAG, "Cannot close profile object");
    }
    ESP_RETURN_ON_ERROR(send_chunk(req, "]}"), TAG, "Cannot close profiles JSON");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t config_handler(httpd_req_t *req) {
    char form[FORM_BODY_MAX];
    char mode[12] = {};
    if (read_form_body(req, form, sizeof(form)) != ESP_OK ||
        !get_form_value(form, "mode", mode, sizeof(mode))) {
        return send_action_json(req, false, "Config access mode is required.");
    }

    esp_err_t ret = config_access_set_mode_from_name(mode, true);
    if (ret != ESP_OK) {
        return send_action_json(req, false, "Invalid config access mode.");
    }

    if (strcmp(mode, "captive") == 0) {
        return send_action_json(
            req, true,
            "Captive portal saved. It can route Mac traffic to ESP next time config mode starts.");
    }

    return send_action_json(
        req, true, "Local-only mode saved. Mac Wi-Fi stays online next time config mode starts.");
}

static esp_err_t led_handler(httpd_req_t *req) {
    char form[FORM_BODY_MAX];
    char mode[12] = {};
    if (read_form_body(req, form, sizeof(form)) != ESP_OK ||
        !get_form_value(form, "mode", mode, sizeof(mode))) {
        return send_action_json(req, false, "LED mode is required.");
    }

    bool save_mode = strcmp(mode, "identify") != 0;
    esp_err_t ret = status_led_set_mode_from_name(mode, save_mode);
    if (ret != ESP_OK) {
        return send_action_json(req, false, "Invalid LED mode.");
    }

    return send_action_json(req, true, "LED mode updated.");
}

static bool read_wifi_credentials_form(httpd_req_t *req, char *ssid, size_t ssid_len, char *password,
                                       size_t password_len) {
    char form[FORM_BODY_MAX];

    if (read_form_body(req, form, sizeof(form)) != ESP_OK ||
        !get_form_value(form, "ssid", ssid, ssid_len) || ssid[0] == '\0') {
        return false;
    }
    get_form_value(form, "password", password, password_len);
    return true;
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    char ssid[33] = {};
    char password[65] = {};

    if (!read_wifi_credentials_form(req, ssid, sizeof(ssid), password, sizeof(password))) {
        return send_action_json(req, false, "SSID is required.");
    }

    esp_err_t ret = persist_wifi_credentials(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi credentials for SSID %s: %s", ssid,
                 esp_err_to_name(ret));
        return send_action_json(req, false, "Wi-Fi credentials could not be saved.");
    }

    ESP_LOGI(TAG, "Wi-Fi credentials saved for SSID %s", ssid);
    return send_action_json(req, true, "Wi-Fi credentials saved.");
}

static esp_err_t test_wifi_handler(httpd_req_t *req) {
    char ssid[33] = {};
    char password[65] = {};

    if (!read_wifi_credentials_form(req, ssid, sizeof(ssid), password, sizeof(password))) {
        return send_action_json(req, false, "SSID is required.");
    }

    esp_err_t ret = start_wifi_connection_attempt(ssid, password, WIFI_CONNECT_ACTION_TEST);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi test for SSID %s: %s", ssid, esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Wi-Fi test requested for SSID %s", ssid);
    return send_connection_response(req);
}

static esp_err_t reconnect_wifi_handler(httpd_req_t *req) {
    char ssid[33] = {};
    char password[65] = {};

    if (!read_wifi_credentials_form(req, ssid, sizeof(ssid), password, sizeof(password))) {
        return send_action_json(req, false, "SSID is required.");
    }

    esp_err_t ret = start_wifi_connection_attempt(ssid, password, WIFI_CONNECT_ACTION_RECONNECT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi reconnect for SSID %s: %s", ssid,
                 esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Wi-Fi reconnect requested for SSID %s", ssid);
    return send_connection_response(req);
}

static esp_err_t profile_connect_handler(httpd_req_t *req) {
    char form[FORM_BODY_MAX];
    size_t profile_id = 0;
    wifi_profile_t profile = {};

    if (read_form_body(req, form, sizeof(form)) != ESP_OK ||
        !get_form_index(form, "id", &profile_id)) {
        return send_action_json(req, false, "Profile id is required.");
    }

    esp_err_t ret = wifi_profiles_get(profile_id, &profile);
    if (ret != ESP_OK) {
        return send_action_json(req, false, "Saved Wi-Fi profile was not found.");
    }

    ret = start_wifi_connection_attempt(profile.ssid, profile.password, WIFI_CONNECT_ACTION_RECONNECT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi profile validation %u: %s", (unsigned)profile_id,
                 esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Wi-Fi profile validation requested for SSID %s", profile.ssid);
    return send_connection_response(req);
}

static esp_err_t connect_status_handler(httpd_req_t *req) {
    return send_connection_response(req);
}

static esp_err_t profile_delete_handler(httpd_req_t *req) {
    char form[FORM_BODY_MAX];
    size_t profile_id = 0;
    if (read_form_body(req, form, sizeof(form)) != ESP_OK ||
        !get_form_index(form, "id", &profile_id)) {
        return send_action_json(req, false, "Profile id is required.");
    }

    esp_err_t ret = wifi_profiles_delete(profile_id);
    if (ret != ESP_OK) {
        return send_action_json(req, false, "Saved Wi-Fi profile was not found.");
    }

    return send_action_json(req, true, "Saved profile removed.");
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
};

static const httpd_uri_t status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = status_handler,
};

static const httpd_uri_t coredump_download_uri = {
    .uri = "/api/coredump",
    .method = HTTP_GET,
    .handler = coredump_download_handler,
};

static const httpd_uri_t coredump_erase_uri = {
    .uri = "/api/coredump/erase",
    .method = HTTP_POST,
    .handler = coredump_erase_handler,
};

static const httpd_uri_t scan_status_uri = {
    .uri = "/api/wifi/scan",
    .method = HTTP_GET,
    .handler = scan_status_handler,
};

static const httpd_uri_t scan_start_uri = {
    .uri = "/api/wifi/scan",
    .method = HTTP_POST,
    .handler = scan_start_handler,
};

static const httpd_uri_t wifi_save_uri = {
    .uri = "/api/wifi/save",
    .method = HTTP_POST,
    .handler = save_wifi_handler,
};

static const httpd_uri_t wifi_test_uri = {
    .uri = "/api/wifi/test",
    .method = HTTP_POST,
    .handler = test_wifi_handler,
};

static const httpd_uri_t wifi_reconnect_uri = {
    .uri = "/api/wifi/reconnect",
    .method = HTTP_POST,
    .handler = reconnect_wifi_handler,
};

static const httpd_uri_t connection_status_uri = {
    .uri = "/api/wifi/connection",
    .method = HTTP_GET,
    .handler = connect_status_handler,
};

static const httpd_uri_t profiles_uri = {
    .uri = "/api/wifi/profiles",
    .method = HTTP_GET,
    .handler = profiles_handler,
};

static const httpd_uri_t profile_connect_uri = {
    .uri = "/api/wifi/profile/connect",
    .method = HTTP_POST,
    .handler = profile_connect_handler,
};

static const httpd_uri_t profile_delete_uri = {
    .uri = "/api/wifi/profile/delete",
    .method = HTTP_POST,
    .handler = profile_delete_handler,
};

static const httpd_uri_t config_uri = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = config_handler,
};

static const httpd_uri_t led_uri = {
    .uri = "/api/led",
    .method = HTTP_POST,
    .handler = led_handler,
};

static esp_err_t start_mdns(esp_netif_t *config_netif) {
    if (s_mdns_started) {
        return ESP_OK;
    }

    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mdns_started = true;

    mdns_txt_item_t service_txt[] = {
        {"path", "/"},
        {"mode", "configuration"},
    };

    ESP_RETURN_ON_ERROR(mdns_hostname_set(CONFIG_MDNS_HOSTNAME), TAG, "Cannot set mDNS hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("ESP32-S2 WiFi Bridge"), TAG,
                        "Cannot set mDNS instance");
    ESP_RETURN_ON_ERROR(mdns_service_add("ESP32-S2 WiFi Bridge", "_http", "_tcp", 80, service_txt,
                                         sizeof(service_txt) / sizeof(service_txt[0])),
                        TAG, "Cannot add mDNS HTTP service");

    if (config_netif) {
        ret = mdns_register_netif(config_netif);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "mDNS custom netif registration failed: %s", esp_err_to_name(ret));
        } else {
            ret = mdns_netif_action(config_netif, MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "mDNS custom netif enable failed: %s", esp_err_to_name(ret));
            }
        }
    }

    ESP_LOGI(TAG, "mDNS config panel available at http://%s", CONFIG_MDNS_HOST_FQDN);
    return ESP_OK;
}

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&s_web_server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &root_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &status_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &coredump_download_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &coredump_erase_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &scan_status_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &scan_start_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &wifi_save_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &wifi_test_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &wifi_reconnect_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &connection_status_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &profiles_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &profile_connect_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &profile_delete_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &config_uri));
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_web_server, &led_uri));
    }
}

esp_err_t start_provisioning(EventGroupHandle_t *flags, int success_bit,
                             config_access_mode_t access_mode, esp_netif_t *config_netif) {
    s_provisioning_flags = flags ? *flags : NULL;
    s_provisioning_success_bit = success_bit;
    s_active_access_mode = access_mode;
    ESP_RETURN_ON_ERROR(init_scan_resources(), TAG, "Cannot allocate Wi-Fi scan state");
    ESP_RETURN_ON_ERROR(init_connect_resources(), TAG, "Cannot allocate Wi-Fi connect state");

    start_webserver();
    esp_err_t mdns_ret = start_mdns(config_netif);
    if (mdns_ret != ESP_OK) {
        ESP_LOGW(TAG, "Continuing without mDNS: %s", esp_err_to_name(mdns_ret));
    }
    if (config_access_mode_uses_dns(access_mode)) {
        dns_server_config_t config =
            DNS_SERVER_CONFIG_SINGLE("wifi.settings" /* name */, "wired" /* USB netif ID */);
        start_dns_server(&config);
    }

    return ESP_OK;
}
