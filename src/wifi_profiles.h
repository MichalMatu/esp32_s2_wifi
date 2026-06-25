#pragma once

#include <stddef.h>

#include "esp_err.h"

#define WIFI_PROFILE_MAX 8
#define WIFI_PROFILE_SSID_LEN 32
#define WIFI_PROFILE_PASSWORD_LEN 64

typedef struct {
    char ssid[WIFI_PROFILE_SSID_LEN + 1];
    char password[WIFI_PROFILE_PASSWORD_LEN + 1];
} wifi_profile_t;

esp_err_t wifi_profiles_load(wifi_profile_t *profiles, size_t max_profiles, size_t *count);
esp_err_t wifi_profiles_save(const char *ssid, const char *password);
esp_err_t wifi_profiles_get(size_t index, wifi_profile_t *profile);
esp_err_t wifi_profiles_delete(size_t index);
