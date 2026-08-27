#include "config_access_policy.h"

#include <string.h>

bool config_access_mode_is_valid(config_access_mode_t mode) {
    return mode >= CONFIG_ACCESS_MODE_LOCAL_ONLY && mode <= CONFIG_ACCESS_MODE_CAPTIVE;
}

bool config_access_mode_parse(const char *name, config_access_mode_t *mode) {
    if (name == NULL || mode == NULL) {
        return false;
    }
    if (strcmp(name, "local") == 0) {
        *mode = CONFIG_ACCESS_MODE_LOCAL_ONLY;
        return true;
    }
    if (strcmp(name, "captive") == 0) {
        *mode = CONFIG_ACCESS_MODE_CAPTIVE;
        return true;
    }
    return false;
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
