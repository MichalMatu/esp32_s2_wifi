#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "form_profile_policy.h"

static void test_form_value_decoding(void) {
    char out[32];

    assert(form_value_get("ssid=My+WiFi", "ssid", out, sizeof(out)));
    assert(strcmp(out, "My WiFi") == 0);

    assert(form_value_get("path=%2Fapi%2fwifi", "path", out, sizeof(out)));
    assert(strcmp(out, "/api/wifi") == 0);

    assert(form_value_get("mode=", "mode", out, sizeof(out)));
    assert(strcmp(out, "") == 0);
}

static void test_form_value_rejects_malformed_input(void) {
    char out[8] = "stale";

    assert(!form_value_get("value=%", "value", out, sizeof(out)));
    assert(strcmp(out, "") == 0);
    assert(!form_value_get("value=%2", "value", out, sizeof(out)));
    assert(!form_value_get("value=%GG", "value", out, sizeof(out)));
    assert(!form_value_get("value=a%00b", "value", out, sizeof(out)));
    assert(!form_value_get("value=12345678", "value", out, sizeof(out)));
    assert(!form_value_get("other=x", "value", out, sizeof(out)));

    assert(!form_value_get(NULL, "value", out, sizeof(out)));
    assert(!form_value_get("value=x", NULL, out, sizeof(out)));
    assert(!form_value_get("value=x", "value", NULL, sizeof(out)));
    assert(!form_value_get("value=x", "value", out, 0));
}

static void test_form_key_matching(void) {
    char out[16];

    assert(form_value_get("userid=9&id=3&id=4", "id", out, sizeof(out)));
    assert(strcmp(out, "3") == 0);

    assert(form_value_get("x=1&&id=7&tail=1", "id", out, sizeof(out)));
    assert(strcmp(out, "7") == 0);

    assert(!form_value_get("profile_id=2", "id", out, sizeof(out)));
}

static void test_profile_index_parsing(void) {
    size_t index = 99;

    assert(form_profile_index_get("id=0", "id", 8, &index));
    assert(index == 0);
    assert(form_profile_index_get("id=7", "id", 8, &index));
    assert(index == 7);
    assert(form_profile_index_get("id=0007", "id", 8, &index));
    assert(index == 7);

    assert(!form_profile_index_get("id=8", "id", 8, &index));
    assert(!form_profile_index_get("id=-1", "id", 8, &index));
    assert(!form_profile_index_get("id=+1", "id", 8, &index));
    assert(!form_profile_index_get("id=1x", "id", 8, &index));
    assert(!form_profile_index_get("id=", "id", 8, &index));
    assert(!form_profile_index_get("id=999999999999999999999999999999", "id", 8, &index));
    assert(!form_profile_index_get("id=1", "id", 0, &index));
    assert(!form_profile_index_get("id=1", "id", 8, NULL));
}

static void test_profile_index_formatting(void) {
    char out[FORM_PROFILE_INDEX_TEXT_MAX];
    char small[2];

    assert(form_profile_index_format(0, 8, out, sizeof(out)));
    assert(strcmp(out, "0") == 0);
    assert(form_profile_index_format(7, 8, out, sizeof(out)));
    assert(strcmp(out, "7") == 0);
    assert(form_profile_index_format(123, 1000, out, sizeof(out)));
    assert(strcmp(out, "123") == 0);

    assert(!form_profile_index_format(8, 8, out, sizeof(out)));
    assert(!form_profile_index_format(0, 0, out, sizeof(out)));
    assert(!form_profile_index_format(12, 100, small, sizeof(small)));
    assert(strcmp(small, "") == 0);
    assert(!form_profile_index_format(0, 8, NULL, sizeof(out)));
    assert(!form_profile_index_format(0, 8, out, 0));
}

int main(void) {
    test_form_value_decoding();
    test_form_value_rejects_malformed_input();
    test_form_key_matching();
    test_profile_index_parsing();
    test_profile_index_formatting();
    return 0;
}
