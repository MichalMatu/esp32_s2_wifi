#include <string.h>

#include "esp_check.h"
#include "nvs.h"

#include "config_access.h"

#define CONFIG_ACCESS_NAMESPACE "config_usb"
#define CONFIG_ACCESS_MODE_KEY "access"
#define CONFIG_ACCESS_SAFE_DEFAULT_KEY "safe_v1"

static const char *TAG = "config_access";

static bool mode_is_valid(config_access_mode_t mode) {
    return mode >= CONFIG_ACCESS_MODE_LOCAL_ONLY && mode <= CONFIG_ACCESS_MODE_CAPTIVE;
}

static esp_err_t save_mode(nvs_handle_t nvs, config_access_mode_t mode) {
    esp_err_t ret = nvs_set_u8(nvs, CONFIG_ACCESS_MODE_KEY, (uint8_t)mode);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs, CONFIG_ACCESS_SAFE_DEFAULT_KEY, 1);
    }
    return ret;
}

config_access_mode_t config_access_get_mode(void) {
    nvs_handle_t nvs;
    uint8_t value = CONFIG_ACCESS_MODE_LOCAL_ONLY;

    if (nvs_open(CONFIG_ACCESS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return CONFIG_ACCESS_MODE_LOCAL_ONLY;
    }

    uint8_t safe_default_applied = 0;
    if (nvs_get_u8(nvs, CONFIG_ACCESS_SAFE_DEFAULT_KEY, &safe_default_applied) != ESP_OK) {
        esp_err_t ret = save_mode(nvs, CONFIG_ACCESS_MODE_LOCAL_ONLY);
        if (ret == ESP_OK) {
            (void)nvs_commit(nvs);
        }
        nvs_close(nvs);
        return CONFIG_ACCESS_MODE_LOCAL_ONLY;
    }

    if (nvs_get_u8(nvs, CONFIG_ACCESS_MODE_KEY, &value) != ESP_OK ||
        !mode_is_valid((config_access_mode_t)value)) {
        value = CONFIG_ACCESS_MODE_LOCAL_ONLY;
        if (save_mode(nvs, CONFIG_ACCESS_MODE_LOCAL_ONLY) == ESP_OK) {
            (void)nvs_commit(nvs);
        }
    }

    nvs_close(nvs);
    return (config_access_mode_t)value;
}

esp_err_t config_access_set_mode(config_access_mode_t mode, bool save) {
    if (!mode_is_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!save) {
        return ESP_OK;
    }

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(CONFIG_ACCESS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "Cannot open config access NVS");
    esp_err_t ret = save_mode(nvs, mode);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

esp_err_t config_access_set_mode_from_name(const char *name, bool save) {
    if (strcmp(name, "local") == 0) {
        return config_access_set_mode(CONFIG_ACCESS_MODE_LOCAL_ONLY, save);
    }
    if (strcmp(name, "captive") == 0) {
        return config_access_set_mode(CONFIG_ACCESS_MODE_CAPTIVE, save);
    }

    return ESP_ERR_INVALID_ARG;
}

bool config_access_mode_uses_gateway(config_access_mode_t mode) {
    return mode == CONFIG_ACCESS_MODE_CAPTIVE;
}

bool config_access_mode_uses_dns(config_access_mode_t mode) {
    return mode == CONFIG_ACCESS_MODE_CAPTIVE;
}

const char *config_access_mode_name(config_access_mode_t mode) {
    switch (mode) {
    case CONFIG_ACCESS_MODE_LOCAL_ONLY:
        return "local";
    case CONFIG_ACCESS_MODE_CAPTIVE:
        return "captive";
    default:
        return "unknown";
    }
}

const char *config_access_mode_label(config_access_mode_t mode) {
    switch (mode) {
    case CONFIG_ACCESS_MODE_LOCAL_ONLY:
        return "Local only";
    case CONFIG_ACCESS_MODE_CAPTIVE:
        return "Captive portal";
    default:
        return "Unknown";
    }
}

const char *config_access_mode_host(config_access_mode_t mode) {
    switch (mode) {
    case CONFIG_ACCESS_MODE_CAPTIVE:
        return "wifi.local / wifi.settings / 192.168.4.1";
    case CONFIG_ACCESS_MODE_LOCAL_ONLY:
    default:
        return "wifi.local / 192.168.4.1";
    }
}
