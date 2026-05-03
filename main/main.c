#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "ssd1306.h"

/* XIAO ESP32-C6 :
 *   SDA → D4 = GPIO22  (fonctionne correctement)
 *   SCL → D8 = GPIO19  (GPIO23/D5 a un pull-down SDIO hardware inutilisable)
 * Branche le fil SCL sur D8, pas D5. */
#define OLED_SDA  22
#define OLED_SCL  19
#define OLED_ADDR 0x3C   /* changer en 0x3D si nécessaire */

static ssd1306_t oled;

void app_main(void)
{
    ssd1306_init(&oled, OLED_SDA, OLED_SCL, OLED_ADDR);

    ssd1306_draw_rect(&oled, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT);
    ssd1306_draw_string(&oled, 14, 1, "XIAO ESP32-C6");
    for (int x = 2; x < SSD1306_WIDTH - 2; x++)
        ssd1306_set_pixel(&oled, x, 18, 1);
    ssd1306_draw_string(&oled, 22, 3, "Hello World!");
    ssd1306_draw_string(&oled, 16, 5, "SSD1306  128x64");
    ssd1306_flush(&oled);

    while (1)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
