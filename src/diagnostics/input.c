#include "diagnostics/input.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

static void mark_changed(diag_input_state_t *state)
{
    state->revision++;
}

static void change_screen(diag_input_state_t *state, int delta)
{
    int screen = state->screen + delta;
    if (screen < 0) {
        screen = DIAG_SCREEN_COUNT - 1;
    } else if (screen >= DIAG_SCREEN_COUNT) {
        screen = 0;
    }
    state->screen = screen;
    mark_changed(state);
}

static void change_menu_item(diag_input_state_t *state, int delta)
{
    state->menu_index += delta;
    if (state->menu_index < 0) {
        state->menu_index = DIAG_MENU_ITEM_COUNT - 1;
    } else if (state->menu_index >= DIAG_MENU_ITEM_COUNT) {
        state->menu_index = 0;
    }
    mark_changed(state);
}

static void handle_encoder_turn(diag_input_state_t *state, int delta)
{
    if (state->menu_open) {
        change_menu_item(state, delta);
    } else {
        change_screen(state, delta);
    }
}

static void select_menu_item(diag_input_state_t *state)
{
    switch (state->menu_index) {
    case 0:
        state->screen = DIAG_SCREEN_STATUS;
        state->menu_open = false;
        break;
    case 1:
        state->screen = DIAG_SCREEN_TRAFFIC;
        state->menu_open = false;
        break;
    case 2:
        state->screen = DIAG_SCREEN_SYSTEM;
        state->menu_open = false;
        break;
    default:
        break;
    }
    mark_changed(state);
}

void diag_input_init(diag_input_state_t *state)
{
    *state = (diag_input_state_t) {
        .screen = DIAG_SCREEN_STATUS,
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
            handle_encoder_turn(state, 1);
            encoder_accum = 0;
        } else if (encoder_accum <= -4) {
            state->encoder_position--;
            handle_encoder_turn(state, -1);
            encoder_accum = 0;
        }
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    int back = gpio_get_level(CONFIG_DIAG_BACK_GPIO);
    int confirm = gpio_get_level(CONFIG_DIAG_CONFIRM_GPIO);
    bool back_pressed = state->back_level == 1 && back == 0;
    bool confirm_pressed = state->confirm_level == 1 && confirm == 0;
    state->back_level = back;
    state->confirm_level = confirm;

    if (now_ms - last_button_ms < 120) {
        return;
    }

    if (back_pressed) {
        state->back_presses++;
        if (state->menu_open) {
            state->menu_open = false;
            mark_changed(state);
        }
        last_button_ms = now_ms;
    } else if (confirm_pressed) {
        state->confirm_presses++;
        if (state->menu_open) {
            select_menu_item(state);
        } else {
            state->menu_index = state->screen;
            state->menu_open = true;
            mark_changed(state);
        }
        last_button_ms = now_ms;
    }
}

#endif
