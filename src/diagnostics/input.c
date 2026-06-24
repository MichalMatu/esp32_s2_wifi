#include "diagnostics/input.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

static int menu_group_item_count(diag_menu_group_t group)
{
    switch (group) {
    case DIAG_MENU_STATUS:
        return 5;
    case DIAG_MENU_WIFI:
        return 5;
    case DIAG_MENU_CONNECT:
        return 4;
    case DIAG_MENU_BRIDGE:
        return 5;
    case DIAG_MENU_TRAFFIC:
        return 4;
    case DIAG_MENU_DIAGNOSTICS:
        return 7;
    case DIAG_MENU_CONFIG:
        return 6;
    case DIAG_MENU_ACTIONS:
        return 6;
    case DIAG_MENU_ABOUT:
        return 4;
    default:
        return 1;
    }
}

static void mark_changed(diag_input_state_t *state)
{
    state->revision++;
}

static void clear_action_confirm(diag_input_state_t *state)
{
    state->action_confirm_armed = false;
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
    state->screen_from_menu = false;
    clear_action_confirm(state);
    mark_changed(state);
}

static void change_menu_item(diag_input_state_t *state, int delta)
{
    int item_count = state->menu_level == DIAG_MENU_ROOT ?
                     DIAG_MENU_GROUP_COUNT :
                     menu_group_item_count(state->menu_group);

    state->menu_index += delta;
    if (state->menu_index < 0) {
        state->menu_index = item_count - 1;
    } else if (state->menu_index >= item_count) {
        state->menu_index = 0;
    }
    mark_changed(state);
}

static void handle_encoder_turn(diag_input_state_t *state, int delta)
{
    if (state->menu_open) {
        change_menu_item(state, delta);
    } else if (state->screen_from_menu) {
        return;
    } else {
        change_screen(state, delta);
    }
}

static diag_menu_group_t screen_to_menu_group(diag_screen_t screen)
{
    switch (screen) {
    case DIAG_SCREEN_WIFI:
        return DIAG_MENU_WIFI;
    case DIAG_SCREEN_CONNECT:
        return DIAG_MENU_CONNECT;
    case DIAG_SCREEN_BRIDGE:
        return DIAG_MENU_BRIDGE;
    case DIAG_SCREEN_TRAFFIC:
        return DIAG_MENU_TRAFFIC;
    case DIAG_SCREEN_DIAGNOSTICS:
        return DIAG_MENU_DIAGNOSTICS;
    case DIAG_SCREEN_CONFIG:
        return DIAG_MENU_CONFIG;
    case DIAG_SCREEN_ACTIONS:
        return DIAG_MENU_ACTIONS;
    case DIAG_SCREEN_ABOUT:
        return DIAG_MENU_ABOUT;
    default:
        return DIAG_MENU_STATUS;
    }
}

static diag_screen_t menu_group_to_screen(diag_menu_group_t group)
{
    switch (group) {
    case DIAG_MENU_WIFI:
        return DIAG_SCREEN_WIFI;
    case DIAG_MENU_CONNECT:
        return DIAG_SCREEN_CONNECT;
    case DIAG_MENU_BRIDGE:
        return DIAG_SCREEN_BRIDGE;
    case DIAG_MENU_TRAFFIC:
        return DIAG_SCREEN_TRAFFIC;
    case DIAG_MENU_DIAGNOSTICS:
        return DIAG_SCREEN_DIAGNOSTICS;
    case DIAG_MENU_CONFIG:
        return DIAG_SCREEN_CONFIG;
    case DIAG_MENU_ACTIONS:
        return DIAG_SCREEN_ACTIONS;
    case DIAG_MENU_ABOUT:
        return DIAG_SCREEN_ABOUT;
    default:
        return DIAG_SCREEN_STATUS;
    }
}

static void select_menu_item(diag_input_state_t *state)
{
    clear_action_confirm(state);
    if (state->menu_level == DIAG_MENU_ROOT) {
        state->menu_group = (diag_menu_group_t)state->menu_index;
        state->menu_level = DIAG_MENU_SUBMENU;
        state->menu_index = 0;
        mark_changed(state);
        return;
    }

    state->screen = menu_group_to_screen(state->menu_group);
    state->detail_index = state->menu_index;
    state->menu_open = false;
    state->screen_from_menu = true;
    mark_changed(state);
}

static diag_action_t action_from_detail_index(int detail_index)
{
    switch (detail_index) {
    case 0:
        return DIAG_ACTION_RECONNECT_WIFI;
    case 2:
        return DIAG_ACTION_RESTART_ESP;
    case 3:
        return DIAG_ACTION_BOOTLOADER;
    default:
        return DIAG_ACTION_NONE;
    }
}

static void handle_action_confirm(diag_input_state_t *state)
{
    diag_action_t action = action_from_detail_index(state->detail_index);

    if (action == DIAG_ACTION_NONE) {
        return;
    }

    if (state->action_confirm_armed) {
        state->action_confirm_armed = false;
        state->action_requested = action;
    } else {
        state->action_confirm_armed = true;
    }
    mark_changed(state);
}

void diag_input_init(diag_input_state_t *state)
{
    *state = (diag_input_state_t) {
        .screen = DIAG_SCREEN_STATUS,
        .menu_level = DIAG_MENU_ROOT,
        .menu_group = DIAG_MENU_STATUS,
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

    if ((back_pressed || confirm_pressed) && now_ms - last_button_ms < 120) {
        return;
    }

    state->back_level = back;
    state->confirm_level = confirm;

    if (back_pressed) {
        state->back_presses++;
        if (state->menu_open) {
            clear_action_confirm(state);
            if (state->menu_level == DIAG_MENU_SUBMENU) {
                state->menu_level = DIAG_MENU_ROOT;
                state->menu_index = state->menu_group;
            } else {
                state->menu_open = false;
            }
            mark_changed(state);
        } else if (state->screen_from_menu) {
            clear_action_confirm(state);
            state->screen_from_menu = false;
            state->menu_open = true;
            state->menu_level = DIAG_MENU_SUBMENU;
            mark_changed(state);
        }
        last_button_ms = now_ms;
    } else if (confirm_pressed) {
        state->confirm_presses++;
        if (state->menu_open) {
            select_menu_item(state);
        } else if (state->screen_from_menu) {
            if (state->screen == DIAG_SCREEN_ACTIONS) {
                handle_action_confirm(state);
            }
        } else {
            clear_action_confirm(state);
            state->screen_from_menu = false;
            state->menu_group = screen_to_menu_group(state->screen);
            state->menu_level = DIAG_MENU_ROOT;
            state->menu_index = state->menu_group;
            state->menu_open = true;
            mark_changed(state);
        }
        last_button_ms = now_ms;
    }
}

#endif
