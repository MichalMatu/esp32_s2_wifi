/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_private/wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "debug_console.h"
#include "provisioning.h"
#include "status_led.h"
#include "wired_iface.h"

static const char *TAG = "example_sta2wired";
#define WIFI_RETRY_INTERVAL_MS 5000

static EventGroupHandle_t s_event_flags;
static bool s_wifi_is_connected = false;
static uint8_t s_sta_mac[6];

const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
const int RECONFIGURE_BIT = BIT2;
const int PROV_SUCCESS_BIT = BIT3;

/**
 * WiFi -- Wired packet path
 */
static esp_err_t wired_recv_callback(void *buffer, uint16_t len, void *ctx) {
    if (s_wifi_is_connected) {
        mac_spoof(FROM_WIRED, buffer, len, s_sta_mac);
        if (esp_wifi_internal_tx(ESP_IF_WIFI_STA, buffer, len) != ESP_OK) {
            debug_console_count_usb_to_wifi_failure();
            ESP_LOGD(TAG, "Failed to send packet to WiFi!");
        }
    }
    return ESP_OK;
}

static void wifi_buff_free(void *buffer, void *ctx) {
    esp_wifi_internal_free_rx_buffer(buffer);
}

static esp_err_t wifi_recv_callback(void *buffer, uint16_t len, void *eb) {
    mac_spoof(TO_WIRED, buffer, len, s_sta_mac);
    if (wired_send(buffer, len, eb) != ESP_OK) {
        esp_wifi_internal_free_rx_buffer(eb);
        debug_console_count_wifi_to_usb_failure();
        ESP_LOGD(TAG, "Failed to send packet to USB!");
    }
    return ESP_OK;
}

static void request_wifi_connect(const char *source) {
    status_led_set_state(STATUS_LED_STATE_CONNECTING);
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "%s Wi-Fi connect request failed: %s", source, esp_err_to_name(ret));
        status_led_set_state(STATUS_LED_STATE_DISCONNECTED);
    }
}

static void wifi_retry_task(void *arg) {
    (void)arg;

    while (true) {
        EventBits_t bits =
            xEventGroupWaitBits(s_event_flags, CONNECTED_BIT | RECONFIGURE_BIT, pdFALSE, pdFALSE,
                                pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS));
        if (bits & RECONFIGURE_BIT) {
            vTaskDelete(NULL);
        }
        if (bits & CONNECTED_BIT) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS));
            continue;
        }
        if (!(bits & CONNECTED_BIT)) {
            request_wifi_connect("periodic retry");
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                          void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = disconnected ? disconnected->reason : 0;
        int8_t rssi = disconnected ? disconnected->rssi : 0;
        ESP_LOGI(TAG, "Wi-Fi STA disconnected reason=%u rssi=%d", (unsigned)reason, (int)rssi);
        s_wifi_is_connected = false;
        debug_console_set_wifi_connected(false);
        debug_console_record_disconnect(reason, rssi);
        status_led_set_state(STATUS_LED_STATE_DISCONNECTED);
        esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, NULL);
        request_wifi_connect("disconnect retry");

        xEventGroupClearBits(s_event_flags, CONNECTED_BIT);
        xEventGroupSetBits(s_event_flags, DISCONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi STA connected");
        esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, wifi_recv_callback);
        s_wifi_is_connected = true;
        debug_console_set_wifi_connected(true);
        status_led_set_state(STATUS_LED_STATE_CONNECTED);
        xEventGroupClearBits(s_event_flags, DISCONNECTED_BIT);
        xEventGroupSetBits(s_event_flags, CONNECTED_BIT);
    }
}

static esp_err_t start_bridge_wifi(void) {
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    status_led_set_state(STATUS_LED_STATE_CONNECTING);

    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        // configuration not available, report error to restart provisioning
        return ESP_FAIL;
    }
    if (wifi_cfg.sta.ssid[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi STA SSID is empty");
        return ESP_ERR_INVALID_STATE;
    }

    request_wifi_connect("initial");

    BaseType_t task_created = xTaskCreate(wifi_retry_task, "wifi_retry", 3072, NULL, 4, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Cannot create Wi-Fi retry task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/**
 * GPIO button functionality
 */
#define GPIO_INPUT CONFIG_EXAMPLE_RECONFIGURE_BUTTON
#define GPIO_LONG_PUSH_MS 2000
#define GPIO_POLL_MS 50

static void button_task(void *arg) {
    (void)arg;

    bool was_pressed = false;
    bool reconfigure_armed = false;
    TickType_t pressed_at = 0;
    status_led_state_t previous_state = STATUS_LED_STATE_BOOT;

    while (true) {
        bool pressed = gpio_get_level(GPIO_INPUT) == 0;
        TickType_t now = xTaskGetTickCount();

        if (pressed && !was_pressed) {
            pressed_at = now;
            reconfigure_armed = false;
            previous_state = status_led_get_state();
            status_led_set_state(STATUS_LED_STATE_BUTTON_HELD);
        }

        if (pressed && !reconfigure_armed && now - pressed_at >= pdMS_TO_TICKS(GPIO_LONG_PUSH_MS)) {
            reconfigure_armed = true;
            status_led_set_state(STATUS_LED_STATE_RECONFIG_REQUESTED);
        }

        if (!pressed && was_pressed) {
            if (reconfigure_armed) {
                xEventGroupSetBits(s_event_flags, RECONFIGURE_BIT);
            } else {
                status_led_set_state(previous_state);
            }
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(GPIO_POLL_MS));
    }
}

static void gpio_init(void) {
    gpio_config_t io_conf = {.intr_type = GPIO_INTR_DISABLE,
                             .pin_bit_mask = (1ULL << GPIO_INPUT),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&io_conf);

    BaseType_t task_created = xTaskCreate(button_task, "boot_button", 2048, NULL, 4, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Cannot create BOOT button task");
    }
}

/**
 * Application
 */
void app_main(void) {
    static __NOINIT_ATTR uint32_t s_reconfigure_requested;
    static const uint32_t RECONFIGURE_REQUEST = 0x1C55AA;

    /* Check reset reason and decide if we should re-provision */
    bool do_provision = false;
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", reason);
    if (reason == ESP_RST_SW && s_reconfigure_requested == RECONFIGURE_REQUEST) {
        do_provision = true;
    }
    s_reconfigure_requested = 0;

    /* Initialize NVS and WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    esp_err_t led_ret = status_led_init();
    if (led_ret != ESP_OK) {
        ESP_LOGW(TAG, "Status LED disabled: %s", esp_err_to_name(led_ret));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // init the flags and event loop
    s_event_flags = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(debug_console_start(s_event_flags, RECONFIGURE_BIT));

    /* Init the re-provisioning button (long-press with initiate provisioning restart) */
    gpio_init();
    esp_read_mac(s_sta_mac, ESP_MAC_WIFI_STA);

    /* Start the application in configuration mode (to perform provisioning)
     * or in a bridge mode (already provisioned) */
    if (do_provision || !is_provisioned()) {
        ESP_LOGI(TAG, "Starting provisioning");
        status_led_set_state(STATUS_LED_STATE_CONFIG);
        config_access_mode_t access_mode = config_access_get_mode();
        ESP_ERROR_CHECK(esp_netif_init());
        // needed to complete provisioning with getting a valid IP event
        esp_netif_create_default_wifi_sta();

        // starts the wired interface with virtual network used to configure/provision the example
        esp_netif_t *config_netif = NULL;
        ESP_ERROR_CHECK(wired_netif_init(access_mode, &config_netif));
        ESP_ERROR_CHECK(
            start_provisioning(&s_event_flags, PROV_SUCCESS_BIT, access_mode, config_netif));
    } else {
        ESP_LOGI(TAG, "Starting USB-WiFi bridge");
        status_led_set_state(STATUS_LED_STATE_CONNECTING);
        if (start_bridge_wifi() != ESP_OK) {
            // missing or invalid Wi-Fi configuration still needs provisioning
            status_led_set_state(STATUS_LED_STATE_DISCONNECTED);
            xEventGroupSetBits(s_event_flags, RECONFIGURE_BIT);
        } else {
            EventBits_t bits =
                xEventGroupWaitBits(s_event_flags, CONNECTED_BIT | RECONFIGURE_BIT, pdFALSE,
                                    pdFALSE, portMAX_DELAY);
            if ((bits & CONNECTED_BIT) && !(bits & RECONFIGURE_BIT)) {
                ESP_LOGI(TAG, "WiFi station connected successfully");
                wired_bridge_init(wired_recv_callback, wifi_buff_free);
            }
        }
    }

    EventBits_t bits = xEventGroupWaitBits(s_event_flags, RECONFIGURE_BIT | PROV_SUCCESS_BIT,
                                           pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & RECONFIGURE_BIT) {
        // retry provisioning if requested by the button press
        s_reconfigure_requested = RECONFIGURE_REQUEST;
    } else {
        // provisioning successfully finished, restart to the bridge mode
        s_reconfigure_requested = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // to let httpd handle the closure
    esp_restart();
}
