#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES  (SSD1306_HEIGHT / 8)

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t fb[SSD1306_WIDTH * SSD1306_PAGES]; /* 1024 bytes framebuffer */
} ssd1306_t;

esp_err_t ssd1306_init(ssd1306_t *oled, i2c_master_bus_handle_t bus, uint8_t addr);
void      ssd1306_clear(ssd1306_t *oled);
void      ssd1306_flush(ssd1306_t *oled);
void      ssd1306_set_pixel(ssd1306_t *oled, int x, int y, int on);
void      ssd1306_draw_char(ssd1306_t *oled, int x, int page, char c);
void      ssd1306_draw_string(ssd1306_t *oled, int x, int page, const char *s);
void      ssd1306_draw_rect(ssd1306_t *oled, int x, int y, int w, int h);
