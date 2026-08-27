#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "config_access_policy.h"

config_access_mode_t config_access_get_mode(void);
esp_err_t config_access_set_mode(config_access_mode_t mode, bool save);
esp_err_t config_access_set_mode_from_name(const char *name, bool save);
