#include "esp_check.h"
#include "nvs.h"

#include "config_access.h"

#define CONFIG_ACCESS_NAMESPACE "config_usb"
#define CONFIG_ACCESS_MODE_KEY "access"
#define CONFIG_ACCESS_SAFE_DEFAULT_KEY "safe_v1"

static const char *TAG = "config_access";

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
        !config_access_mode_is_valid((config_access_mode_t)value)) {
        value = CONFIG_ACCESS_MODE_LOCAL_ONLY;
        if (save_mode(nvs, CONFIG_ACCESS_MODE_LOCAL_ONLY) == ESP_OK) {
            (void)nvs_commit(nvs);
        }
    }

    nvs_close(nvs);
    return (config_access_mode_t)value;
}

esp_err_t config_access_set_mode(config_access_mode_t mode, bool save) {
    if (!config_access_mode_is_valid(mode)) {
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
    config_access_mode_t mode;
    if (!config_access_mode_parse(name, &mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    return config_access_set_mode(mode, save);
}
