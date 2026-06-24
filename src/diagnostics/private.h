#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "diagnostics.h"

#define DIAG_PAGE_COUNT 3

typedef struct {
    diag_mode_t mode;
    bool wifi_connected;
    uint64_t usb_to_wifi_bytes;
    uint64_t wifi_to_usb_bytes;
    uint32_t usb_to_wifi_packets;
    uint32_t wifi_to_usb_packets;
} diag_state_t;

typedef struct {
    int page;
    int32_t encoder_position;
    uint32_t back_presses;
    uint32_t confirm_presses;
    int encoder_a_level;
    int encoder_b_level;
    int back_level;
    int confirm_level;
} diag_input_state_t;
