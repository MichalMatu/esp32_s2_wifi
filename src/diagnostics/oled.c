#include "diagnostics/oled.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_DIAG_OLED_ENABLED

#define OLED_I2C_TIMEOUT_MS 100
#define OLED_I2C_SPEED_HZ 100000

static const char *TAG = "diagnostics_oled";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_oled_dev;
static uint8_t s_fb[DIAG_OLED_WIDTH * DIAG_OLED_PAGES];

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

bool diag_oled_init(uint8_t *address)
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

    uint8_t found_address = oled_find_address();
    if (found_address == 0) {
        return false;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = found_address,
        .scl_speed_hz = OLED_I2C_SPEED_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_oled_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OLED device 0x%02X: %s", found_address, esp_err_to_name(ret));
        return false;
    }

    ret = oled_apply_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init command failed: %s", esp_err_to_name(ret));
        return false;
    }

    diag_oled_clear();
    if (!diag_oled_flush()) {
        ESP_LOGE(TAG, "OLED flush failed after init");
        return false;
    }

    if (address) {
        *address = found_address;
    }
    ESP_LOGI(TAG, "OLED diagnostics display ready");
    return true;
}

bool diag_oled_flush(void)
{
    for (int page = 0; page < DIAG_OLED_PAGES; page++) {
        if (oled_cmd(0xB0 | page) != ESP_OK ||
            oled_cmd(0x02) != ESP_OK ||
            oled_cmd(0x10) != ESP_OK ||
            oled_data(&s_fb[page * DIAG_OLED_WIDTH], DIAG_OLED_WIDTH) != ESP_OK) {
            return false;
        }
    }
    return true;
}

void diag_oled_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void diag_oled_draw_text(int line, int x, const char *text)
{
    if (line < 0 || line >= DIAG_OLED_PAGES || x >= DIAG_OLED_WIDTH) {
        return;
    }

    uint8_t *row = &s_fb[line * DIAG_OLED_WIDTH];
    while (*text && x + 5 < DIAG_OLED_WIDTH) {
        const uint8_t *font = font_get(*text++);
        for (int i = 0; i < 5; i++) {
            row[x++] = font[i];
        }
        if (x < DIAG_OLED_WIDTH) {
            row[x++] = 0x00;
        }
    }
}

#endif
