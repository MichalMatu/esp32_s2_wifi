#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    STATUS_LED_MODE_OFF = 0,
    STATUS_LED_MODE_ON,
    STATUS_LED_MODE_STATUS,
    STATUS_LED_MODE_IDENTIFY,
} status_led_mode_t;

typedef enum {
    STATUS_LED_STATE_BOOT = 0,
    STATUS_LED_STATE_CONFIG,
    STATUS_LED_STATE_CONNECTING,
    STATUS_LED_STATE_CONNECTED,
    STATUS_LED_STATE_DISCONNECTED,
    STATUS_LED_STATE_SAVING,
    STATUS_LED_STATE_BUTTON_HELD,
    STATUS_LED_STATE_RECONFIG_REQUESTED,
} status_led_state_t;

esp_err_t status_led_init(void);
esp_err_t status_led_set_mode(status_led_mode_t mode, bool save);
esp_err_t status_led_set_mode_from_name(const char *name, bool save);
status_led_mode_t status_led_get_mode(void);
const char *status_led_get_mode_name(status_led_mode_t mode);
const char *status_led_get_mode_label(status_led_mode_t mode);

void status_led_set_state(status_led_state_t state);
status_led_state_t status_led_get_state(void);
const char *status_led_get_state_name(status_led_state_t state);
