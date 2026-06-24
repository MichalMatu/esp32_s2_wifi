#include "diagnostics/input.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

static void change_page(diag_input_state_t *state, int delta)
{
    state->page += delta;
    if (state->page < 0) {
        state->page = DIAG_PAGE_COUNT - 1;
    } else if (state->page >= DIAG_PAGE_COUNT) {
        state->page = 0;
    }
}

void diag_input_init(diag_input_state_t *state)
{
    *state = (diag_input_state_t) {
        .encoder_a_level = 1,
        .encoder_b_level = 1,
        .back_level = 1,
        .confirm_level = 1,
    };

    uint64_t mask = (1ULL << CONFIG_DIAG_ENCODER_A_GPIO) |
                    (1ULL << CONFIG_DIAG_ENCODER_B_GPIO) |
                    (1ULL << CONFIG_DIAG_BACK_GPIO) |
                    (1ULL << CONFIG_DIAG_CONFIRM_GPIO);

    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

void diag_input_poll(diag_input_state_t *state)
{
    static int last_encoder_state = -1;
    static int encoder_accum;
    static int64_t last_button_ms;
    static const int8_t transitions[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0
    };

    int a = gpio_get_level(CONFIG_DIAG_ENCODER_A_GPIO);
    int b = gpio_get_level(CONFIG_DIAG_ENCODER_B_GPIO);
    int encoder_state = (a << 1) | b;
    state->encoder_a_level = a;
    state->encoder_b_level = b;

    if (last_encoder_state < 0) {
        last_encoder_state = encoder_state;
    } else if (encoder_state != last_encoder_state) {
        int index = (last_encoder_state << 2) | encoder_state;
        encoder_accum += transitions[index & 0x0F];
        last_encoder_state = encoder_state;

        if (encoder_accum >= 4) {
            state->encoder_position++;
            change_page(state, 1);
            encoder_accum = 0;
        } else if (encoder_accum <= -4) {
            state->encoder_position--;
            change_page(state, -1);
            encoder_accum = 0;
        }
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_button_ms < 180) {
        return;
    }

    int back = gpio_get_level(CONFIG_DIAG_BACK_GPIO);
    int confirm = gpio_get_level(CONFIG_DIAG_CONFIRM_GPIO);
    state->back_level = back;
    state->confirm_level = confirm;

    if (back == 0) {
        state->back_presses++;
        change_page(state, -1);
        last_button_ms = now_ms;
    } else if (confirm == 0) {
        state->confirm_presses++;
        change_page(state, 1);
        last_button_ms = now_ms;
    }
}

#endif
