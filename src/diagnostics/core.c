#include "diagnostics.h"

#include "diagnostics/input.h"
#include "diagnostics/oled.h"
#include "diagnostics/private.h"
#include "diagnostics/ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

static const char *TAG = "diagnostics";

static diag_state_t s_state = {
    .mode = DIAG_MODE_BOOT,
};
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_display_ready;
static uint8_t s_oled_address;
static diag_input_state_t s_input_state;

static void snapshot(diag_state_t *out)
{
    portENTER_CRITICAL(&s_state_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}

static void oled_render_runtime(void)
{
    static int64_t last_render_ms;
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (now_ms - last_render_ms >= CONFIG_DIAG_OLED_UPDATE_MS) {
        diag_state_t state;
        snapshot(&state);
        diag_ui_render(&state, &s_input_state);
        last_render_ms = now_ms;
    }
}

static void diagnostics_task(void *arg)
{
    (void)arg;
    diag_input_init(&s_input_state);

    int64_t last_probe_ms = -3000;
    int64_t last_status_ms = 0;
    while (true) {
        diag_input_poll(&s_input_state);

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
    xTaskCreate(diagnostics_task, "diagnostics", 4096, NULL, 4, NULL);
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
