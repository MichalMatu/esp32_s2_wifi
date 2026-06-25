#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#include "wifi_profiles.h"

#define WIFI_PROFILES_NAMESPACE "wifi_profiles"
#define WIFI_PROFILES_KEY "profiles"
#define WIFI_PROFILES_MAGIC 0x57504631U
#define WIFI_PROFILES_VERSION 1

static const char *TAG = "wifi_profiles";

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    wifi_profile_t profiles[WIFI_PROFILE_MAX];
} wifi_profile_store_t;

static void init_store(wifi_profile_store_t *store) {
    memset(store, 0, sizeof(*store));
    store->magic = WIFI_PROFILES_MAGIC;
    store->version = WIFI_PROFILES_VERSION;
}

static bool store_is_valid(const wifi_profile_store_t *store) {
    return store->magic == WIFI_PROFILES_MAGIC && store->version == WIFI_PROFILES_VERSION &&
           store->count <= WIFI_PROFILE_MAX;
}

static esp_err_t read_store(wifi_profile_store_t *store) {
    init_store(store);

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(WIFI_PROFILES_NAMESPACE, NVS_READONLY, &nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Cannot open Wi-Fi profiles NVS");

    size_t blob_size = sizeof(*store);
    ret = nvs_get_blob(nvs, WIFI_PROFILES_KEY, store, &blob_size);
    nvs_close(nvs);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        init_store(store);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Cannot read Wi-Fi profiles");

    if (blob_size != sizeof(*store) || !store_is_valid(store)) {
        ESP_LOGW(TAG, "Ignoring invalid Wi-Fi profile store");
        init_store(store);
    }

    return ESP_OK;
}

static esp_err_t write_store(const wifi_profile_store_t *store) {
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_PROFILES_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "Cannot open Wi-Fi profiles NVS for write");

    esp_err_t ret = nvs_set_blob(nvs, WIFI_PROFILES_KEY, store, sizeof(*store));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

esp_err_t wifi_profiles_load(wifi_profile_t *profiles, size_t max_profiles, size_t *count) {
    if (!profiles || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_profile_store_t store;
    ESP_RETURN_ON_ERROR(read_store(&store), TAG, "Cannot load Wi-Fi profiles");

    size_t copy_count = store.count < max_profiles ? store.count : max_profiles;
    memcpy(profiles, store.profiles, copy_count * sizeof(wifi_profile_t));
    *count = copy_count;
    return ESP_OK;
}

esp_err_t wifi_profiles_save(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > WIFI_PROFILE_SSID_LEN ||
        (password && strlen(password) > WIFI_PROFILE_PASSWORD_LEN)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_profile_store_t store;
    ESP_RETURN_ON_ERROR(read_store(&store), TAG, "Cannot load Wi-Fi profiles before save");

    wifi_profile_t updated = {};
    strlcpy(updated.ssid, ssid, sizeof(updated.ssid));
    if (password) {
        strlcpy(updated.password, password, sizeof(updated.password));
    }

    size_t existing = WIFI_PROFILE_MAX;
    for (size_t i = 0; i < store.count; i++) {
        if (strcmp(store.profiles[i].ssid, ssid) == 0) {
            existing = i;
            break;
        }
    }

    size_t old_count = store.count;
    if (existing < old_count) {
        for (size_t i = existing; i > 0; i--) {
            store.profiles[i] = store.profiles[i - 1];
        }
    } else {
        if (store.count < WIFI_PROFILE_MAX) {
            store.count++;
        }
        for (size_t i = store.count - 1; i > 0; i--) {
            store.profiles[i] = store.profiles[i - 1];
        }
    }

    store.profiles[0] = updated;
    return write_store(&store);
}

esp_err_t wifi_profiles_get(size_t index, wifi_profile_t *profile) {
    if (!profile) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_profile_store_t store;
    ESP_RETURN_ON_ERROR(read_store(&store), TAG, "Cannot load Wi-Fi profile");
    if (index >= store.count) {
        return ESP_ERR_NOT_FOUND;
    }

    *profile = store.profiles[index];
    return ESP_OK;
}

esp_err_t wifi_profiles_delete(size_t index) {
    wifi_profile_store_t store;
    ESP_RETURN_ON_ERROR(read_store(&store), TAG, "Cannot load Wi-Fi profiles before delete");
    if (index >= store.count) {
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = index; i + 1 < store.count; i++) {
        store.profiles[i] = store.profiles[i + 1];
    }
    store.count--;
    memset(&store.profiles[store.count], 0, sizeof(store.profiles[store.count]));

    return write_store(&store);
}
