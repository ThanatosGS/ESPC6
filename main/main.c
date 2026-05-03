#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include "aht21.h"
#include "ens160.h"

/* XIAO ESP32-C6 : SDA=D4(GPIO22)  SCL=D8(GPIO19)
 * GPIO23/D5 inutilisable (pull-down SDIO hardware) */
#define I2C_SDA 22
#define I2C_SCL 19

static const char *const aqi_label[] = {
    "---", "Excellent", "Good", "Moderate", "Poor", "Unhealthy"
};

static ssd1306_t oled;
static aht21_t   aht;
static ens160_t  ens;

void app_main(void)
{
    gpio_reset_pin(I2C_SDA);
    gpio_reset_pin(I2C_SCL);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_0,
        .sda_io_num          = I2C_SDA,
        .scl_io_num          = I2C_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    ssd1306_init(&oled, bus, 0x3C);
    aht21_init(&aht, bus);
    ens160_init(&ens, bus);

    /* splash screen */
    ssd1306_draw_rect(&oled, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT);
    ssd1306_draw_string(&oled, 14, 1, "XIAO ESP32-C6");
    ssd1306_draw_string(&oled, 16, 3, "Air Quality");
    ssd1306_draw_string(&oled, 28, 5, "Monitor");
    ssd1306_flush(&oled);
    vTaskDelay(pdMS_TO_TICKS(2000));

    char line[22];
    while (1) {
        float temp = 25.0f, hum = 50.0f;
        ens160_data_t air = {0};

        aht21_read(&aht, &temp, &hum);
        ens160_set_compensation(&ens, temp, hum);
        ens160_read(&ens, &air);

        uint8_t aqi_idx = (air.aqi >= 1 && air.aqi <= 5) ? air.aqi : 0;

        ssd1306_clear(&oled);
        ssd1306_draw_rect(&oled, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT);

        /* temp + humidity  — page 1 (y=8-15) */
        snprintf(line, sizeof(line), "T:%.1fC  H:%.0f%%", temp, hum);
        ssd1306_draw_string(&oled, 4, 1, line);

        /* separator line at y=18 */
        for (int x = 2; x < SSD1306_WIDTH - 2; x++)
            ssd1306_set_pixel(&oled, x, 18, 1);

        /* air quality — pages 3-5 (y=24-47) */
        snprintf(line, sizeof(line), "CO2:  %5u ppm", air.eco2);
        ssd1306_draw_string(&oled, 4, 3, line);

        snprintf(line, sizeof(line), "TVOC: %5u ppb", air.tvoc);
        ssd1306_draw_string(&oled, 4, 4, line);

        snprintf(line, sizeof(line), "AQI:%u %s", aqi_idx, aqi_label[aqi_idx]);
        ssd1306_draw_string(&oled, 4, 5, line);

        ssd1306_flush(&oled);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
