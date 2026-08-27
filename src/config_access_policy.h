#pragma once

#include <stdbool.h>

typedef enum {
    CONFIG_ACCESS_MODE_LOCAL_ONLY = 0,
    CONFIG_ACCESS_MODE_CAPTIVE,
} config_access_mode_t;

bool config_access_mode_is_valid(config_access_mode_t mode);
bool config_access_mode_parse(const char *name, config_access_mode_t *mode);
bool config_access_mode_uses_gateway(config_access_mode_t mode);
bool config_access_mode_uses_dns(config_access_mode_t mode);
const char *config_access_mode_name(config_access_mode_t mode);
const char *config_access_mode_label(config_access_mode_t mode);
const char *config_access_mode_host(config_access_mode_t mode);
