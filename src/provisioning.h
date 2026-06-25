/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"

#include "config_access.h"

#define PROVISIONING_SCAN_MAX_RESULTS 12

typedef enum {
    PROVISIONING_SCAN_STATE_IDLE,
    PROVISIONING_SCAN_STATE_RUNNING,
    PROVISIONING_SCAN_STATE_DONE,
    PROVISIONING_SCAN_STATE_ERROR,
} provisioning_scan_state_t;

typedef struct {
    uint32_t seq;
    provisioning_scan_state_t state;
    uint16_t total;
    uint16_t count;
    uint32_t duration_ms;
    char error[80];
    wifi_ap_record_t records[PROVISIONING_SCAN_MAX_RESULTS];
} provisioning_scan_snapshot_t;

/**
 * @brief Checks if the device has been provisioned
 * @return true if WiFi is provisioned
 */
bool is_provisioned(void);

/**
 * @brief Initiate provisioning
 * @param flags Event flags to indicate status of provisioning
 * @param success_bit bits set in the event flags on success
 * @return ESP_OK if provisioning started
 */
esp_err_t start_provisioning(EventGroupHandle_t *flags, int success_bit,
                             config_access_mode_t access_mode, esp_netif_t *config_netif);

const char *provisioning_scan_state_name(provisioning_scan_state_t state);
const char *provisioning_wifi_auth_mode_name(wifi_auth_mode_t auth_mode);
esp_err_t provisioning_scan_start(void);
esp_err_t provisioning_scan_get_snapshot(provisioning_scan_snapshot_t *snapshot);
