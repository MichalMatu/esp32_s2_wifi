#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "diagnostics.h"

typedef enum {
    DIAG_SCREEN_STATUS = 0,
    DIAG_SCREEN_WIFI,
    DIAG_SCREEN_CONNECT,
    DIAG_SCREEN_BRIDGE,
    DIAG_SCREEN_TRAFFIC,
    DIAG_SCREEN_DIAGNOSTICS,
    DIAG_SCREEN_CONFIG,
    DIAG_SCREEN_ACTIONS,
    DIAG_SCREEN_ABOUT,
    DIAG_SCREEN_COUNT,
} diag_screen_t;

typedef enum {
    DIAG_MENU_ROOT = 0,
    DIAG_MENU_SUBMENU,
} diag_menu_level_t;

typedef enum {
    DIAG_MENU_STATUS = 0,
    DIAG_MENU_WIFI,
    DIAG_MENU_CONNECT,
    DIAG_MENU_BRIDGE,
    DIAG_MENU_TRAFFIC,
    DIAG_MENU_DIAGNOSTICS,
    DIAG_MENU_CONFIG,
    DIAG_MENU_ACTIONS,
    DIAG_MENU_ABOUT,
    DIAG_MENU_GROUP_COUNT,
} diag_menu_group_t;

typedef struct {
    diag_mode_t mode;
    bool wifi_connected;
    uint64_t usb_to_wifi_bytes;
    uint64_t wifi_to_usb_bytes;
    uint32_t usb_to_wifi_packets;
    uint32_t wifi_to_usb_packets;
} diag_state_t;

typedef struct {
    diag_screen_t screen;
    bool menu_open;
    bool screen_from_menu;
    diag_menu_level_t menu_level;
    diag_menu_group_t menu_group;
    int menu_index;
    uint32_t revision;
    int32_t encoder_position;
    uint32_t back_presses;
    uint32_t confirm_presses;
    int encoder_a_level;
    int encoder_b_level;
    int back_level;
    int confirm_level;
} diag_input_state_t;
