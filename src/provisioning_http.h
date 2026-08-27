#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#include "wifi_profiles.h"

void provisioning_http_prepare_json_response(httpd_req_t *req);
esp_err_t provisioning_http_send_chunk(httpd_req_t *req, const char *text);
esp_err_t provisioning_http_send_json_string(httpd_req_t *req, const char *text);
esp_err_t provisioning_http_send_json_field(httpd_req_t *req, const char *key, const char *value,
                                             bool trailing_comma);
esp_err_t provisioning_http_send_json_bool_field(httpd_req_t *req, const char *key, bool value,
                                                  bool trailing_comma);
esp_err_t provisioning_http_send_json_uint_field(httpd_req_t *req, const char *key, uint32_t value,
                                                  bool trailing_comma);
esp_err_t provisioning_http_send_json_nullable_int_field(httpd_req_t *req, const char *key,
                                                          int value, bool has_value,
                                                          bool trailing_comma);
esp_err_t provisioning_http_send_action_json(httpd_req_t *req, bool ok, const char *message);
esp_err_t provisioning_http_send_profiles_error(httpd_req_t *req, const char *message);
esp_err_t provisioning_http_send_profiles(httpd_req_t *req, const wifi_profile_t *profiles,
                                            size_t profile_count, size_t max_profiles);

bool provisioning_http_read_form_value(httpd_req_t *req, const char *key, char *out,
                                        size_t out_len);
bool provisioning_http_read_profile_index(httpd_req_t *req, const char *key,
                                           size_t upper_bound_exclusive, size_t *out);
bool provisioning_http_read_wifi_credentials(httpd_req_t *req, char *ssid, size_t ssid_len,
                                               char *password, size_t password_len);
