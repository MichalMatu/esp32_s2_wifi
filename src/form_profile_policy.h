#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FORM_PROFILE_INDEX_TEXT_MAX (3U * sizeof(size_t) + 1U)

bool form_value_get(const char *form, const char *key, char *out, size_t out_len);
bool form_profile_index_get(const char *form, const char *key, size_t upper_bound_exclusive,
                            size_t *out);
bool form_profile_index_format(size_t index, size_t upper_bound_exclusive, char *out,
                               size_t out_len);
