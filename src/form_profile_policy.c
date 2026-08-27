#include "form_profile_policy.h"

#include <string.h>

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool decode_form_component(const char *start, const char *end, char *out, size_t out_len) {
    if (start == NULL || end == NULL || out == NULL || out_len == 0 || end < start) {
        return false;
    }

    size_t written = 0;
    for (const char *cursor = start; cursor < end; cursor++) {
        char decoded = *cursor;
        if (*cursor == '+') {
            decoded = ' ';
        } else if (*cursor == '%') {
            if (cursor + 2 >= end) {
                return false;
            }
            int high = hex_digit(cursor[1]);
            int low = hex_digit(cursor[2]);
            if (high < 0 || low < 0) {
                return false;
            }
            decoded = (char)((high << 4) | low);
            cursor += 2;
        }

        if (decoded == '\0' || written + 1 >= out_len) {
            return false;
        }
        out[written++] = decoded;
    }
    out[written] = '\0';
    return true;
}

bool form_value_get(const char *form, const char *key, char *out, size_t out_len) {
    if (form == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }

    out[0] = '\0';
    size_t key_len = strlen(key);
    const char *cursor = form;
    while (*cursor) {
        const char *field_end = strchr(cursor, '&');
        if (field_end == NULL) {
            field_end = cursor + strlen(cursor);
        }

        const char *equals = memchr(cursor, '=', (size_t)(field_end - cursor));
        if (equals != NULL && (size_t)(equals - cursor) == key_len &&
            strncmp(cursor, key, key_len) == 0) {
            if (!decode_form_component(equals + 1, field_end, out, out_len)) {
                out[0] = '\0';
                return false;
            }
            return true;
        }

        cursor = *field_end == '&' ? field_end + 1 : field_end;
    }
    return false;
}

bool form_profile_index_get(const char *form, const char *key, size_t upper_bound_exclusive,
                            size_t *out) {
    char value[FORM_PROFILE_INDEX_TEXT_MAX];
    if (out == NULL || upper_bound_exclusive == 0 ||
        !form_value_get(form, key, value, sizeof(value)) || value[0] == '\0') {
        return false;
    }

    size_t parsed = 0;
    const size_t max_allowed = upper_bound_exclusive - 1;
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        size_t digit = (size_t)(*cursor - '0');
        if (parsed > max_allowed / 10 ||
            (parsed == max_allowed / 10 && digit > max_allowed % 10)) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    *out = parsed;
    return true;
}

bool form_profile_index_format(size_t index, size_t upper_bound_exclusive, char *out,
                               size_t out_len) {
    if (out == NULL || out_len == 0 || upper_bound_exclusive == 0 ||
        index >= upper_bound_exclusive) {
        return false;
    }

    char reversed[FORM_PROFILE_INDEX_TEXT_MAX];
    size_t length = 0;
    do {
        reversed[length++] = (char)('0' + (index % 10));
        index /= 10;
    } while (index != 0);

    if (length + 1 > out_len) {
        out[0] = '\0';
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        out[i] = reversed[length - i - 1];
    }
    out[length] = '\0';
    return true;
}
