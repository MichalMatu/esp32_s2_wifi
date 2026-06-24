#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "diagnostics.h"

#define DIAG_WIFI_SCAN_RESULTS 5
#define DIAG_WIFI_SSID_LEN 33

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

typedef enum {
    DIAG_ACTION_NONE = 0,
    DIAG_ACTION_SCAN_WIFI,
    DIAG_ACTION_RECONNECT_WIFI,
    DIAG_ACTION_RESTART_BRIDGE,
    DIAG_ACTION_RESTART_ESP,
    DIAG_ACTION_BOOTLOADER,
    DIAG_ACTION_RESET_WIFI_CONFIG,
    DIAG_ACTION_FACTORY_RESET,
} diag_action_t;

typedef struct {
    char ssid[DIAG_WIFI_SSID_LEN];
    int8_t rssi;
    uint8_t channel;
    int auth_mode;
} diag_wifi_scan_result_t;

typedef struct {
    diag_mode_t mode;
    bool wifi_connected;
    uint64_t usb_to_wifi_bytes;
    uint64_t wifi_to_usb_bytes;
    uint32_t usb_to_wifi_packets;
    uint32_t wifi_to_usb_packets;
    bool wifi_scan_running;
    uint16_t wifi_scan_count;
    uint16_t wifi_scan_total;
    int wifi_scan_error;
    diag_wifi_scan_result_t wifi_scan_results[DIAG_WIFI_SCAN_RESULTS];
} diag_state_t;

typedef struct {
    diag_screen_t screen;
    bool menu_open;
    bool screen_from_menu;
    diag_menu_level_t menu_level;
    diag_menu_group_t menu_group;
    int menu_index;
    int detail_index;
    bool action_confirm_armed;
    diag_action_t action_requested;
    uint32_t revision;
    int32_t encoder_position;
    uint32_t back_presses;
    uint32_t confirm_presses;
    int encoder_a_level;
    int encoder_b_level;
    int back_level;
    int confirm_level;
} diag_input_state_t;
