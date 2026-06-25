#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "status_led.h"

#ifndef CONFIG_STATUS_LED_GPIO
#define CONFIG_STATUS_LED_GPIO 15
#endif

#define STATUS_LED_NAMESPACE "status_led"
#define STATUS_LED_MODE_KEY "mode"
#define STATUS_LED_ON_LEVEL 1
#define STATUS_LED_OFF_LEVEL 0

static const char *TAG = "status_led";

static volatile status_led_mode_t s_mode = STATUS_LED_MODE_STATUS;
static volatile status_led_state_t s_state = STATUS_LED_STATE_BOOT;

static esp_err_t set_led(bool on) {
    return gpio_set_level(CONFIG_STATUS_LED_GPIO, on ? STATUS_LED_ON_LEVEL : STATUS_LED_OFF_LEVEL);
}

static bool mode_is_valid(status_led_mode_t mode) {
    return mode >= STATUS_LED_MODE_OFF && mode <= STATUS_LED_MODE_IDENTIFY;
}

static status_led_mode_t load_saved_mode(void) {
    nvs_handle_t nvs;
    uint8_t value = STATUS_LED_MODE_STATUS;

    if (nvs_open(STATUS_LED_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return STATUS_LED_MODE_STATUS;
    }

    if (nvs_get_u8(nvs, STATUS_LED_MODE_KEY, &value) != ESP_OK ||
        !mode_is_valid((status_led_mode_t)value) ||
        (status_led_mode_t)value == STATUS_LED_MODE_IDENTIFY) {
        value = STATUS_LED_MODE_STATUS;
    }

    nvs_close(nvs);
    return (status_led_mode_t)value;
}

static esp_err_t save_mode(status_led_mode_t mode) {
    nvs_handle_t nvs;

    if (mode == STATUS_LED_MODE_IDENTIFY) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(nvs_open(STATUS_LED_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "Cannot open LED NVS");
    esp_err_t ret = nvs_set_u8(nvs, STATUS_LED_MODE_KEY, (uint8_t)mode);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

static bool status_pattern_on(status_led_state_t state, uint32_t tick) {
    switch (state) {
    case STATUS_LED_STATE_BUTTON_HELD:
        return true;
    case STATUS_LED_STATE_RECONFIG_REQUESTED:
        return (tick % 4) < 2;
    case STATUS_LED_STATE_CONFIG:
        return (tick % 20) < 5;
    case STATUS_LED_STATE_CONNECTING:
        return (tick % 6) < 3;
    case STATUS_LED_STATE_CONNECTED:
        return (tick % 30) < 1;
    case STATUS_LED_STATE_DISCONNECTED:
        return (tick % 10) < 2;
    case STATUS_LED_STATE_SAVING:
        return (tick % 4) < 2;
    case STATUS_LED_STATE_BOOT:
    default:
        return (tick % 8) < 4;
    }
}

static void status_led_task(void *arg) {
    (void)arg;

    bool last_on = true;
    uint32_t tick = 0;

    while (true) {
        status_led_mode_t mode = s_mode;
        status_led_state_t state = s_state;
        bool on = false;

        if (mode == STATUS_LED_MODE_ON) {
            on = true;
        } else if (mode == STATUS_LED_MODE_IDENTIFY) {
            on = (tick % 4) < 2;
        } else if (mode == STATUS_LED_MODE_STATUS) {
            on = status_pattern_on(state, tick);
        }

        if (on != last_on) {
            set_led(on);
            last_on = on;
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t status_led_init(void) {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(CONFIG_STATUS_LED_GPIO), TAG, "Cannot reset LED GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_direction(CONFIG_STATUS_LED_GPIO, GPIO_MODE_OUTPUT), TAG,
                        "Cannot set LED GPIO direction");
    ESP_RETURN_ON_ERROR(set_led(false), TAG, "Cannot clear LED GPIO");

    s_mode = load_saved_mode();

    BaseType_t task_created = xTaskCreate(status_led_task, "status_led", 3072, NULL, 3, NULL);
    if (task_created != pdPASS) {
        ESP_LOGW(TAG, "Cannot create status LED task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GPIO status LED initialized on GPIO %d", CONFIG_STATUS_LED_GPIO);
    return ESP_OK;
}

esp_err_t status_led_set_mode(status_led_mode_t mode, bool save) {
    if (!mode_is_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_mode = mode;
    if (save) {
        return save_mode(mode);
    }

    return ESP_OK;
}

esp_err_t status_led_set_mode_from_name(const char *name, bool save) {
    if (strcmp(name, "off") == 0) {
        return status_led_set_mode(STATUS_LED_MODE_OFF, save);
    }
    if (strcmp(name, "on") == 0) {
        return status_led_set_mode(STATUS_LED_MODE_ON, save);
    }
    if (strcmp(name, "status") == 0) {
        return status_led_set_mode(STATUS_LED_MODE_STATUS, save);
    }
    if (strcmp(name, "identify") == 0) {
        return status_led_set_mode(STATUS_LED_MODE_IDENTIFY, false);
    }

    return ESP_ERR_INVALID_ARG;
}

status_led_mode_t status_led_get_mode(void) {
    return s_mode;
}

const char *status_led_get_mode_name(status_led_mode_t mode) {
    switch (mode) {
    case STATUS_LED_MODE_OFF:
        return "off";
    case STATUS_LED_MODE_ON:
        return "on";
    case STATUS_LED_MODE_STATUS:
        return "status";
    case STATUS_LED_MODE_IDENTIFY:
        return "identify";
    default:
        return "unknown";
    }
}

const char *status_led_get_mode_label(status_led_mode_t mode) {
    switch (mode) {
    case STATUS_LED_MODE_OFF:
        return "Off";
    case STATUS_LED_MODE_ON:
        return "On";
    case STATUS_LED_MODE_STATUS:
        return "Status";
    case STATUS_LED_MODE_IDENTIFY:
        return "Identify";
    default:
        return "Unknown";
    }
}

void status_led_set_state(status_led_state_t state) {
    s_state = state;
}

status_led_state_t status_led_get_state(void) {
    return s_state;
}

const char *status_led_get_state_name(status_led_state_t state) {
    switch (state) {
    case STATUS_LED_STATE_BOOT:
        return "boot";
    case STATUS_LED_STATE_CONFIG:
        return "configuration";
    case STATUS_LED_STATE_CONNECTING:
        return "connecting";
    case STATUS_LED_STATE_CONNECTED:
        return "connected";
    case STATUS_LED_STATE_DISCONNECTED:
        return "disconnected";
    case STATUS_LED_STATE_SAVING:
        return "saving";
    case STATUS_LED_STATE_BUTTON_HELD:
        return "button-held";
    case STATUS_LED_STATE_RECONFIG_REQUESTED:
        return "reconfig-requested";
    default:
        return "unknown";
    }
}
