#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

esp_err_t debug_console_start(EventGroupHandle_t flags, int reconfigure_bit);
void debug_console_set_wifi_connected(bool connected);
void debug_console_record_disconnect(uint8_t reason, int8_t rssi);
void debug_console_count_wifi_to_usb_failure(void);
void debug_console_count_usb_to_wifi_failure(void);
