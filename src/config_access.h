#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    CONFIG_ACCESS_MODE_LOCAL_ONLY = 0,
    CONFIG_ACCESS_MODE_CAPTIVE,
} config_access_mode_t;

config_access_mode_t config_access_get_mode(void);
esp_err_t config_access_set_mode(config_access_mode_t mode, bool save);
esp_err_t config_access_set_mode_from_name(const char *name, bool save);

bool config_access_mode_uses_gateway(config_access_mode_t mode);
bool config_access_mode_uses_dns(config_access_mode_t mode);
const char *config_access_mode_name(config_access_mode_t mode);
const char *config_access_mode_label(config_access_mode_t mode);
const char *config_access_mode_host(config_access_mode_t mode);
