#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DIAG_OLED_WIDTH 128
#define DIAG_OLED_PAGES 8

bool diag_oled_init(uint8_t *address);
bool diag_oled_flush(void);
void diag_oled_clear(void);
void diag_oled_draw_text(int line, int x, const char *text);
