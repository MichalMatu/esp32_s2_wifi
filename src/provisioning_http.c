#include "provisioning_http.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "form_profile_policy.h"

#define FORM_BODY_MAX 512

static const char *TAG = "provisioning_http";

esp_err_t provisioning_http_send_chunk(httpd_req_t *req, const char *text) {
    if (req == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return httpd_resp_sendstr_chunk(req, text);
}

esp_err_t provisioning_http_send_json_string(httpd_req_t *req, const char *text) {
    if (req == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "\""), TAG, "Cannot send JSON quote");

    const char *start = text;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        const char *replacement = NULL;
        char unicode_escape[7];

        switch (*cursor) {
        case '\\':
            replacement = "\\\\";
            break;
        case '"':
            replacement = "\\\"";
            break;
        case '\b':
            replacement = "\\b";
            break;
        case '\f':
            replacement = "\\f";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            if (*cursor < 0x20) {
                snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", *cursor);
                replacement = unicode_escape;
            }
            break;
        }

        if (replacement) {
            const char *current = (const char *)cursor;
            if (current > start) {
                ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, start, current - start), TAG,
                                    "Cannot send JSON chunk");
            }
            ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, replacement), TAG,
                                "Cannot send JSON escape");
            cursor++;
            start = (const char *)cursor;
        } else {
            cursor++;
        }
    }

    if ((const char *)cursor > start) {
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, start, (const char *)cursor - start), TAG,
                            "Cannot send JSON tail");
    }
    return provisioning_http_send_chunk(req, "\"");
}

esp_err_t provisioning_http_send_json_field(httpd_req_t *req, const char *key, const char *value,
                                             bool trailing_comma) {
    if (req == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ":"), TAG, "Cannot send JSON colon");
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, value), TAG,
                        "Cannot send JSON value");
    if (trailing_comma) {
        return provisioning_http_send_chunk(req, ",");
    }
    return ESP_OK;
}

esp_err_t provisioning_http_send_json_bool_field(httpd_req_t *req, const char *key, bool value,
                                                  bool trailing_comma) {
    if (req == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, value ? ":true" : ":false"), TAG,
                        "Cannot send JSON bool");
    if (trailing_comma) {
        return provisioning_http_send_chunk(req, ",");
    }
    return ESP_OK;
}

esp_err_t provisioning_http_send_json_uint_field(httpd_req_t *req, const char *key, uint32_t value,
                                                  bool trailing_comma) {
    if (req == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char text[16];
    snprintf(text, sizeof(text), "%" PRIu32, value);
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ":"), TAG, "Cannot send JSON colon");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, text), TAG, "Cannot send JSON uint");
    if (trailing_comma) {
        return provisioning_http_send_chunk(req, ",");
    }
    return ESP_OK;
}

esp_err_t provisioning_http_send_json_nullable_int_field(httpd_req_t *req, const char *key,
                                                          int value, bool has_value,
                                                          bool trailing_comma) {
    if (req == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, key), TAG, "Cannot send JSON key");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ":"), TAG, "Cannot send JSON colon");
    if (has_value) {
        char text[16];
        snprintf(text, sizeof(text), "%d", value);
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, text), TAG, "Cannot send JSON int");
    } else {
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "null"), TAG, "Cannot send JSON null");
    }
    if (trailing_comma) {
        return provisioning_http_send_chunk(req, ",");
    }
    return ESP_OK;
}

void provisioning_http_prepare_json_response(httpd_req_t *req) {
    if (req == NULL) {
        return;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

esp_err_t provisioning_http_send_action_json(httpd_req_t *req, bool ok, const char *message) {
    if (req == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    provisioning_http_prepare_json_response(req);
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "{\"ok\":"), TAG,
                        "Cannot send action JSON");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ok ? "true," : "false,"), TAG,
                        "Cannot send action state");
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_field(req, "message", message, false), TAG,
                        "Cannot send action message");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "}"), TAG, "Cannot close action JSON");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t provisioning_http_send_profiles_error(httpd_req_t *req, const char *message) {
    if (req == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    provisioning_http_prepare_json_response(req);
    ESP_RETURN_ON_ERROR(
        provisioning_http_send_chunk(req,
                                     "{\"source\":\"device\",\"ok\":false,\"profiles\":[],"
                                     "\"error\":"),
        TAG, "Cannot send profiles error");
    ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, message), TAG,
                        "Cannot send profiles error message");
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "}"), TAG,
                        "Cannot close profiles error");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t provisioning_http_send_profiles(httpd_req_t *req, const wifi_profile_t *profiles,
                                            size_t profile_count, size_t max_profiles) {
    if (req == NULL || max_profiles == 0 || (profile_count > 0 && profiles == NULL) ||
        profile_count > max_profiles) {
        return ESP_ERR_INVALID_ARG;
    }

    provisioning_http_prepare_json_response(req);
    ESP_RETURN_ON_ERROR(
        provisioning_http_send_chunk(req, "{\"source\":\"device\",\"ok\":true,\"profiles\":["),
        TAG, "Cannot send profiles JSON");
    for (size_t i = 0; i < profile_count; i++) {
        char id[FORM_PROFILE_INDEX_TEXT_MAX];
        if (!form_profile_index_format(i, max_profiles, id, sizeof(id))) {
            ESP_LOGE(TAG, "Cannot format profile id %u", (unsigned)i);
            return ESP_FAIL;
        }
        if (i > 0) {
            ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ","), TAG,
                                "Cannot send profile comma");
        }
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "{\"id\":"), TAG,
                            "Cannot send profile object");
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, id), TAG, "Cannot send profile id");
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ",\"ssid\":"), TAG,
                            "Cannot send profile SSID key");
        ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, profiles[i].ssid), TAG,
                            "Cannot send profile SSID");
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, ",\"password\":"), TAG,
                            "Cannot send profile password key");
        ESP_RETURN_ON_ERROR(provisioning_http_send_json_string(req, profiles[i].password), TAG,
                            "Cannot send profile password");
        ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "}"), TAG,
                            "Cannot close profile object");
    }
    ESP_RETURN_ON_ERROR(provisioning_http_send_chunk(req, "]}"), TAG,
                        "Cannot close profiles JSON");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t read_form_body(httpd_req_t *req, char *out, size_t out_len) {
    if (req == NULL || out == NULL || out_len == 0 || req->content_len == 0 ||
        req->content_len >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, out + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    out[received] = '\0';
    return ESP_OK;
}

bool provisioning_http_read_form_value(httpd_req_t *req, const char *key, char *out,
                                        size_t out_len) {
    if (req == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }
    char form[FORM_BODY_MAX];
    return read_form_body(req, form, sizeof(form)) == ESP_OK &&
           form_value_get(form, key, out, out_len);
}

bool provisioning_http_read_profile_index(httpd_req_t *req, const char *key,
                                           size_t upper_bound_exclusive, size_t *out) {
    if (req == NULL || key == NULL || upper_bound_exclusive == 0 || out == NULL) {
        return false;
    }
    char form[FORM_BODY_MAX];
    return read_form_body(req, form, sizeof(form)) == ESP_OK &&
           form_profile_index_get(form, key, upper_bound_exclusive, out);
}

bool provisioning_http_read_wifi_credentials(httpd_req_t *req, char *ssid, size_t ssid_len,
                                               char *password, size_t password_len) {
    if (req == NULL || ssid == NULL || ssid_len == 0 || password == NULL || password_len == 0) {
        return false;
    }

    char form[FORM_BODY_MAX];
    if (read_form_body(req, form, sizeof(form)) != ESP_OK ||
        !form_value_get(form, "ssid", ssid, ssid_len) || ssid[0] == '\0') {
        return false;
    }
    (void)form_value_get(form, "password", password, password_len);
    return true;
}
