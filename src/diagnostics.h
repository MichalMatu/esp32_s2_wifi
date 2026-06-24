#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIAG_MODE_BOOT = 0,
    DIAG_MODE_CONFIG,
    DIAG_MODE_BRIDGE,
} diag_mode_t;

void diagnostics_start(void);
void diagnostics_set_mode(diag_mode_t mode);
void diagnostics_set_wifi_connected(bool connected);
void diagnostics_add_usb_to_wifi(uint16_t len);
void diagnostics_add_wifi_to_usb(uint16_t len);
