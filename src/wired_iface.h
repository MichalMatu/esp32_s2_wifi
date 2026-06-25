/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

#include "config_access.h"

typedef esp_err_t (*wired_rx_cb_t)(void *buffer, uint16_t len, void *ctx);

typedef void (*wired_free_cb_t)(void *buffer, void *ctx);

typedef enum { FROM_WIRED, TO_WIRED } mac_spoof_direction_t;

void mac_spoof(mac_spoof_direction_t direction, uint8_t *buffer, uint16_t len,
               const uint8_t own_mac[6]);

esp_err_t wired_bridge_init(wired_rx_cb_t rx_cb, wired_free_cb_t free_cb);

esp_err_t wired_send(void *buffer, uint16_t len, void *buff_free_arg);

esp_err_t wired_netif_init(config_access_mode_t access_mode, esp_netif_t **out_netif);

esp_err_t wired_usb_refresh_link(void);
