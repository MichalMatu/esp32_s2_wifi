#include "diagnostics.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

#define OLED_WIDTH 128
#define OLED_PAGES 8
#define OLED_I2C_TIMEOUT_MS 100
#define OLED_PAGE_COUNT 3
#define OLED_I2C_SPEED_HZ 100000

static const char *TAG = "diagnostics";

typedef struct {
    diag_mode_t mode;
    bool wifi_connected;
    uint64_t usb_to_wifi_bytes;
    uint64_t wifi_to_usb_bytes;
    uint32_t usb_to_wifi_packets;
    uint32_t wifi_to_usb_packets;
} diag_state_t;

static diag_state_t s_state = {
    .mode = DIAG_MODE_BOOT,
};
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_oled_dev;
static bool s_display_ready;
static uint8_t s_oled_address;
static uint8_t s_fb[OLED_WIDTH * OLED_PAGES];
static int s_page;
static int32_t s_encoder_position;
static uint32_t s_back_presses;
static uint32_t s_confirm_presses;
static int s_encoder_a_level = 1;
static int s_encoder_b_level = 1;
static int s_back_level = 1;
static int s_confirm_level = 1;

static const uint8_t FONT_SPACE[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t FONT_EXCL[5] = {0x00, 0x00, 0x5F, 0x00, 0x00};
static const uint8_t FONT_DOT[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
static const uint8_t FONT_SLASH[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
static const uint8_t FONT_COLON[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
static const uint8_t FONT_DASH[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
static const uint8_t FONT_GT[5] = {0x41, 0x22, 0x14, 0x08, 0x00};
static const uint8_t FONT_LT[5] = {0x08, 0x14, 0x22, 0x41, 0x00};
static const uint8_t FONT_QMARK[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
static const uint8_t FONT_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
static const uint8_t FONT_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
static const uint8_t FONT_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
static const uint8_t FONT_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
static const uint8_t FONT_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
static const uint8_t FONT_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
static const uint8_t FONT_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
static const uint8_t FONT_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
static const uint8_t FONT_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
static const uint8_t FONT_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
static const uint8_t FONT_A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
static const uint8_t FONT_B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
static const uint8_t FONT_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
static const uint8_t FONT_D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
static const uint8_t FONT_E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
static const uint8_t FONT_F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
static const uint8_t FONT_G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
static const uint8_t FONT_H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
static const uint8_t FONT_I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
static const uint8_t FONT_J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
static const uint8_t FONT_K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
static const uint8_t FONT_L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
static const uint8_t FONT_M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
static const uint8_t FONT_N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
static const uint8_t FONT_O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
static const uint8_t FONT_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
static const uint8_t FONT_Q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
static const uint8_t FONT_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
static const uint8_t FONT_S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
static const uint8_t FONT_T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
static const uint8_t FONT_U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
static const uint8_t FONT_V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
static const uint8_t FONT_W[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
static const uint8_t FONT_X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
static const uint8_t FONT_Y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
static const uint8_t FONT_Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

static const uint8_t *font_get(char c)
{
    if (c >= 'a' && c <= 'z') {
        c -= 32;
    }

    switch (c) {
    case ' ': return FONT_SPACE;
    case '!': return FONT_EXCL;
    case '.': return FONT_DOT;
    case '/': return FONT_SLASH;
    case ':': return FONT_COLON;
    case '-': return FONT_DASH;
    case '>': return FONT_GT;
    case '<': return FONT_LT;
    case '?': return FONT_QMARK;
    case '0': return FONT_0;
    case '1': return FONT_1;
    case '2': return FONT_2;
    case '3': return FONT_3;
    case '4': return FONT_4;
    case '5': return FONT_5;
    case '6': return FONT_6;
    case '7': return FONT_7;
    case '8': return FONT_8;
    case '9': return FONT_9;
    case 'A': return FONT_A;
    case 'B': return FONT_B;
    case 'C': return FONT_C;
    case 'D': return FONT_D;
    case 'E': return FONT_E;
    case 'F': return FONT_F;
    case 'G': return FONT_G;
    case 'H': return FONT_H;
    case 'I': return FONT_I;
    case 'J': return FONT_J;
    case 'K': return FONT_K;
    case 'L': return FONT_L;
    case 'M': return FONT_M;
    case 'N': return FONT_N;
    case 'O': return FONT_O;
    case 'P': return FONT_P;
    case 'Q': return FONT_Q;
    case 'R': return FONT_R;
    case 'S': return FONT_S;
    case 'T': return FONT_T;
    case 'U': return FONT_U;
    case 'V': return FONT_V;
    case 'W': return FONT_W;
    case 'X': return FONT_X;
    case 'Y': return FONT_Y;
    case 'Z': return FONT_Z;
    default: return FONT_QMARK;
    }
}

static void snapshot(diag_state_t *out)
{
    portENTER_CRITICAL(&s_state_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}

static esp_err_t oled_write(uint8_t control, const uint8_t *data, size_t len)
{
    uint8_t buffer[17];
    size_t offset = 0;

    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > sizeof(buffer) - 1) {
            chunk = sizeof(buffer) - 1;
        }
        buffer[0] = control;
        memcpy(&buffer[1], &data[offset], chunk);
        esp_err_t ret = i2c_master_transmit(s_oled_dev, buffer, chunk + 1, OLED_I2C_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        offset += chunk;
    }
    return ESP_OK;
}

static esp_err_t oled_cmd(uint8_t cmd)
{
    return oled_write(0x00, &cmd, 1);
}

static esp_err_t oled_cmd_list(const uint8_t *cmds, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        esp_err_t ret = oled_cmd(cmds[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t oled_data(const uint8_t *data, size_t len)
{
    return oled_write(0x40, data, len);
}

static bool oled_flush(void)
{
    for (int page = 0; page < OLED_PAGES; page++) {
        if (oled_cmd(0xB0 | page) != ESP_OK ||
            oled_cmd(0x02) != ESP_OK ||
            oled_cmd(0x10) != ESP_OK ||
            oled_data(&s_fb[page * OLED_WIDTH], OLED_WIDTH) != ESP_OK) {
            return false;
        }
    }
    return true;
}

static bool oled_probe_address(uint8_t address)
{
    return i2c_master_probe(s_i2c_bus, address, OLED_I2C_TIMEOUT_MS) == ESP_OK;
}

static uint8_t oled_find_address(void)
{
    uint8_t configured = CONFIG_DIAG_OLED_I2C_ADDR;

    if (oled_probe_address(configured)) {
        ESP_LOGI(TAG, "OLED found at configured I2C address 0x%02X", configured);
        return configured;
    }

    ESP_LOGW(TAG, "No OLED ACK at configured I2C address 0x%02X, scanning bus", configured);
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        if (address == configured) {
            continue;
        }
        if (oled_probe_address(address)) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X, using it for OLED", address);
            return address;
        }
    }

    ESP_LOGE(TAG, "No I2C devices found on SDA=%d SCL=%d",
             CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO);
    return 0;
}

static void fb_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

static void draw_text(int line, int x, const char *text)
{
    if (line < 0 || line >= OLED_PAGES || x >= OLED_WIDTH) {
        return;
    }

    uint8_t *row = &s_fb[line * OLED_WIDTH];
    while (*text && x + 5 < OLED_WIDTH) {
        const uint8_t *font = font_get(*text++);
        for (int i = 0; i < 5; i++) {
            row[x++] = font[i];
        }
        if (x < OLED_WIDTH) {
            row[x++] = 0x00;
        }
    }
}

static void fmt_uptime(char *buf, size_t len)
{
    uint64_t seconds = esp_timer_get_time() / 1000000ULL;
    unsigned hours = seconds / 3600;
    unsigned minutes = (seconds / 60) % 60;
    unsigned secs = seconds % 60;
    snprintf(buf, len, "%02u:%02u:%02u", hours, minutes, secs);
}

static const char *mode_name(diag_mode_t mode)
{
    switch (mode) {
    case DIAG_MODE_CONFIG:
        return "CONFIG";
    case DIAG_MODE_BRIDGE:
        return "BRIDGE";
    default:
        return "BOOT";
    }
}

static void render_status(const diag_state_t *state)
{
    wifi_ap_record_t ap = {};
    wifi_config_t cfg = {};
    char line[32];
    const char *ssid = "-";
    int rssi = 0;
    uint8_t channel = 0;

    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != 0) {
        ssid = (const char *)cfg.sta.ssid;
    }
    if (state->wifi_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
        channel = ap.primary;
    }

    draw_text(0, 0, "ESP32-S2 USB");
    snprintf(line, sizeof(line), "MODE:%s", mode_name(state->mode));
    draw_text(1, 0, line);
    snprintf(line, sizeof(line), "WIFI:%s", state->wifi_connected ? "OK" : "DOWN");
    draw_text(2, 0, line);
    snprintf(line, sizeof(line), "RSSI:%d DBM", rssi);
    draw_text(3, 0, state->wifi_connected ? line : "RSSI:-");
    snprintf(line, sizeof(line), "CH:%u", channel);
    draw_text(4, 0, state->wifi_connected ? line : "CH:-");
    snprintf(line, sizeof(line), "SSID:%.15s", ssid);
    draw_text(5, 0, line);
    draw_text(7, 0, "<B  OK>");
}

static void render_traffic(const diag_state_t *state)
{
    char line[32];

    draw_text(0, 0, "TRAFFIC");
    snprintf(line, sizeof(line), "USB>WIFI:%" PRIu64 "K", state->usb_to_wifi_bytes / 1024ULL);
    draw_text(2, 0, line);
    snprintf(line, sizeof(line), "WIFI>USB:%" PRIu64 "K", state->wifi_to_usb_bytes / 1024ULL);
    draw_text(3, 0, line);
    snprintf(line, sizeof(line), "U2W PKT:%" PRIu32, state->usb_to_wifi_packets);
    draw_text(5, 0, line);
    snprintf(line, sizeof(line), "W2U PKT:%" PRIu32, state->wifi_to_usb_packets);
    draw_text(6, 0, line);
    draw_text(7, 0, "<B  OK>");
}

static void render_system(const diag_state_t *state)
{
    char line[32];
    char uptime[16];

    (void)state;
    fmt_uptime(uptime, sizeof(uptime));

    draw_text(0, 0, "SYSTEM");
    snprintf(line, sizeof(line), "UP:%s", uptime);
    draw_text(2, 0, line);
    snprintf(line, sizeof(line), "HEAP:%" PRIu32, esp_get_free_heap_size());
    draw_text(3, 0, line);
    draw_text(4, 0, "USB:NCM");
    snprintf(line, sizeof(line), "I2C:%d/%d", CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO);
    draw_text(5, 0, line);
    draw_text(7, 0, "<B  OK>");
}

static void render_oled_debug_status(void)
{
    char line[32];

    draw_text(0, 0, "OLED OK");
    snprintf(line, sizeof(line), "ENC:%" PRId32, s_encoder_position);
    draw_text(1, 0, line);
    snprintf(line, sizeof(line), "BACK:%" PRIu32, s_back_presses);
    draw_text(2, 0, line);
    snprintf(line, sizeof(line), "OK:%" PRIu32, s_confirm_presses);
    draw_text(3, 0, line);
    snprintf(line, sizeof(line), "A:%d B:%d", s_encoder_a_level, s_encoder_b_level);
    draw_text(4, 0, line);
    snprintf(line, sizeof(line), "BK:%d PSH:%d", s_back_level, s_confirm_level);
    draw_text(5, 0, line);
    snprintf(line, sizeof(line), "PAGE:%d", s_page);
    draw_text(6, 0, line);
    draw_text(7, 0, "ROT PRESS TEST");
}

static void render_page(void)
{
    fb_clear();
#if CONFIG_APP_OLED_DEBUG_ONLY
    render_oled_debug_status();
#else
    diag_state_t state;
    snapshot(&state);

    switch (s_page) {
    case 1:
        render_traffic(&state);
        break;
    case 2:
        render_system(&state);
        break;
    default:
        render_status(&state);
        break;
    }
#endif
    oled_flush();
}

static esp_err_t oled_apply_init(void)
{
    static const uint8_t oled_init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xFF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF,
    };

    ESP_LOGI(TAG, "Applying OLED init: compatible page mode");
    return oled_cmd_list(oled_init, sizeof(oled_init));
}

static bool oled_init(void)
{
    esp_err_t ret;

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_DIAG_OLED_SDA_GPIO,
        .scl_io_num = CONFIG_DIAG_OLED_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_LOGI(TAG, "Starting OLED I2C: SDA=%d SCL=%d configured_addr=0x%02X",
             CONFIG_DIAG_OLED_SDA_GPIO, CONFIG_DIAG_OLED_SCL_GPIO,
             CONFIG_DIAG_OLED_I2C_ADDR);

    if (!s_i2c_bus) {
        ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            return false;
        }
    }

    uint8_t address = oled_find_address();
    if (address == 0) {
        return false;
    }
    s_oled_address = address;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = OLED_I2C_SPEED_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_oled_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OLED device 0x%02X: %s", address, esp_err_to_name(ret));
        return false;
    }

    ret = oled_apply_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init command failed: %s", esp_err_to_name(ret));
        return false;
    }

    fb_clear();
    if (!oled_flush()) {
        ESP_LOGE(TAG, "OLED flush failed after init");
        return false;
    }

    ESP_LOGI(TAG, "OLED diagnostics display ready");
    return true;
}

static void oled_render_runtime(void)
{
    static int64_t last_render_ms;
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (now_ms - last_render_ms >= CONFIG_DIAG_OLED_UPDATE_MS) {
        render_page();
        last_render_ms = now_ms;
    }
}

static void inputs_init(void)
{
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

static void change_page(int delta)
{
    s_page += delta;
    if (s_page < 0) {
        s_page = OLED_PAGE_COUNT - 1;
    } else if (s_page >= OLED_PAGE_COUNT) {
        s_page = 0;
    }
}

static void poll_inputs(void)
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
    s_encoder_a_level = a;
    s_encoder_b_level = b;

    if (last_encoder_state < 0) {
        last_encoder_state = encoder_state;
    } else if (encoder_state != last_encoder_state) {
        int index = (last_encoder_state << 2) | encoder_state;
        encoder_accum += transitions[index & 0x0F];
        last_encoder_state = encoder_state;

        if (encoder_accum >= 4) {
            s_encoder_position++;
            change_page(1);
            encoder_accum = 0;
        } else if (encoder_accum <= -4) {
            s_encoder_position--;
            change_page(-1);
            encoder_accum = 0;
        }
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_button_ms < 180) {
        return;
    }

    int back = gpio_get_level(CONFIG_DIAG_BACK_GPIO);
    int confirm = gpio_get_level(CONFIG_DIAG_CONFIRM_GPIO);
    s_back_level = back;
    s_confirm_level = confirm;

    if (back == 0) {
        s_back_presses++;
        change_page(-1);
        last_button_ms = now_ms;
    } else if (confirm == 0) {
        s_confirm_presses++;
        change_page(1);
        last_button_ms = now_ms;
    }
}

static void diagnostics_task(void *arg)
{
    (void)arg;
    inputs_init();

    int64_t last_probe_ms = -3000;
    int64_t last_status_ms = 0;
    while (true) {
        poll_inputs();

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (!s_display_ready && now_ms - last_probe_ms >= 3000) {
            s_display_ready = oled_init();
            last_probe_ms = now_ms;
        }

        if (s_display_ready) {
            oled_render_runtime();
        }

        if (s_display_ready && now_ms - last_status_ms >= 5000) {
            ESP_LOGI(TAG, "OLED ready at I2C address 0x%02X", s_oled_address);
            last_status_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void diagnostics_start(void)
{
    xTaskCreate(diagnostics_task, "diagnostics", 4096, NULL, 4, NULL);
}

void diagnostics_set_mode(diag_mode_t mode)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.mode = mode;
    portEXIT_CRITICAL(&s_state_lock);
}

void diagnostics_set_wifi_connected(bool connected)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.wifi_connected = connected;
    portEXIT_CRITICAL(&s_state_lock);
}

void diagnostics_add_usb_to_wifi(uint16_t len)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.usb_to_wifi_bytes += len;
    s_state.usb_to_wifi_packets++;
    portEXIT_CRITICAL(&s_state_lock);
}

void diagnostics_add_wifi_to_usb(uint16_t len)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.wifi_to_usb_bytes += len;
    s_state.wifi_to_usb_packets++;
    portEXIT_CRITICAL(&s_state_lock);
}

#else

void diagnostics_start(void) {}
void diagnostics_set_mode(diag_mode_t mode) { (void)mode; }
void diagnostics_set_wifi_connected(bool connected) { (void)connected; }
void diagnostics_add_usb_to_wifi(uint16_t len) { (void)len; }
void diagnostics_add_wifi_to_usb(uint16_t len) { (void)len; }

#endif
