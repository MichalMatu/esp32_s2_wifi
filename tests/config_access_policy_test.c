#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "config_access_policy.h"

static void test_mode_validation(void) {
    assert(config_access_mode_is_valid(CONFIG_ACCESS_MODE_LOCAL_ONLY));
    assert(config_access_mode_is_valid(CONFIG_ACCESS_MODE_CAPTIVE));
    assert(!config_access_mode_is_valid((config_access_mode_t)-1));
    assert(!config_access_mode_is_valid((config_access_mode_t)2));
}

static void test_mode_parsing(void) {
    config_access_mode_t mode = CONFIG_ACCESS_MODE_CAPTIVE;

    assert(config_access_mode_parse("local", &mode));
    assert(mode == CONFIG_ACCESS_MODE_LOCAL_ONLY);

    assert(config_access_mode_parse("captive", &mode));
    assert(mode == CONFIG_ACCESS_MODE_CAPTIVE);

    assert(!config_access_mode_parse("LOCAL", &mode));
    assert(!config_access_mode_parse("", &mode));
    assert(!config_access_mode_parse(NULL, &mode));
    assert(!config_access_mode_parse("local", NULL));
}

static void test_network_policy(void) {
    assert(!config_access_mode_uses_gateway(CONFIG_ACCESS_MODE_LOCAL_ONLY));
    assert(!config_access_mode_uses_dns(CONFIG_ACCESS_MODE_LOCAL_ONLY));
    assert(config_access_mode_uses_gateway(CONFIG_ACCESS_MODE_CAPTIVE));
    assert(config_access_mode_uses_dns(CONFIG_ACCESS_MODE_CAPTIVE));
}

static void test_display_values(void) {
    assert(strcmp(config_access_mode_name(CONFIG_ACCESS_MODE_LOCAL_ONLY), "local") == 0);
    assert(strcmp(config_access_mode_name(CONFIG_ACCESS_MODE_CAPTIVE), "captive") == 0);
    assert(strcmp(config_access_mode_name((config_access_mode_t)2), "unknown") == 0);

    assert(strcmp(config_access_mode_label(CONFIG_ACCESS_MODE_LOCAL_ONLY), "Local only") == 0);
    assert(strcmp(config_access_mode_label(CONFIG_ACCESS_MODE_CAPTIVE), "Captive portal") == 0);
    assert(strcmp(config_access_mode_label((config_access_mode_t)2), "Unknown") == 0);

    assert(strcmp(config_access_mode_host(CONFIG_ACCESS_MODE_LOCAL_ONLY),
                  "wifi.local / 192.168.4.1") == 0);
    assert(strcmp(config_access_mode_host(CONFIG_ACCESS_MODE_CAPTIVE),
                  "wifi.local / wifi.settings / 192.168.4.1") == 0);
}

int main(void) {
    test_mode_validation();
    test_mode_parsing();
    test_network_policy();
    test_display_values();
    return 0;
}
