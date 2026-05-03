#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "ssd1306.h"
#include "aht21.h"
#include "ens160.h"
#include "zigbee.h"

/* XIAO ESP32-C6 : SDA=D4(GPIO22)  SCL=D8(GPIO19) */
#define I2C_SDA 22
#define I2C_SCL 19

/* Correction du self-heating de l'AHT21 (typiquement -1.5 à -3 °C).
 * Ajuste cette valeur jusqu'à correspondre à un thermomètre de référence. */
#define TEMP_OFFSET_C (-2.0f)

static const char *const aqi_label[] = {
    "---", "Excellent", "Good", "Moderate", "Poor", "Unhealthy"
};

static ssd1306_t oled;
static aht21_t   aht;
static ens160_t  ens;

/* Dernières valeurs capteurs — écrites par sensor_task, lues par on_zigbee_poll.
 * float/uint16 sont atomiques sur RISC-V 32-bit → volatile suffit. */
static volatile float    g_temp  = 25.0f;
static volatile float    g_hum   = 50.0f;
static volatile uint16_t g_eco2  = 0;
static volatile uint16_t g_tvoc  = 0;
static volatile uint8_t  g_aqi   = 0;
static volatile bool     g_valid = false;

/* ----- Affichage --------------------------------------------------------- */

static void update_display(float temp, float hum, const ens160_data_t *air)
{
    char line[22];
    uint8_t aqi_idx = (air->aqi >= 1 && air->aqi <= 5) ? air->aqi : 0;

    ssd1306_clear(&oled);
    ssd1306_draw_rect(&oled, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT);

    snprintf(line, sizeof(line), "T:%.1fC  H:%.0f%%", temp, hum);
    ssd1306_draw_string(&oled, 4, 1, line);

    for (int x = 2; x < SSD1306_WIDTH - 2; x++)
        ssd1306_set_pixel(&oled, x, 18, 1);

    snprintf(line, sizeof(line), "CO2:  %5u ppm", air->eco2);
    ssd1306_draw_string(&oled, 4, 3, line);

    snprintf(line, sizeof(line), "TVOC: %5u ppb", air->tvoc);
    ssd1306_draw_string(&oled, 4, 4, line);

    snprintf(line, sizeof(line), "AQI:%u %s", aqi_idx, aqi_label[aqi_idx]);
    ssd1306_draw_string(&oled, 4, 5, line);

    ssd1306_flush(&oled);
}

/* ----- Tâche capteurs (toutes les 30s) ----------------------------------- */

static void sensor_task(void *pv)
{
    while (1) {
        float temp, hum;
        ens160_data_t air = {0};

        aht21_read(&aht, &temp, &hum);
        temp += TEMP_OFFSET_C;
        ens160_set_compensation(&ens, temp, hum);
        ens160_read(&ens, &air);

        g_temp  = temp;
        g_hum   = hum;
        g_eco2  = air.eco2;
        g_tvoc  = air.tvoc;
        g_aqi   = air.aqi;
        g_valid = true;

        update_display(temp, hum, &air);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* ----- Callback Zigbee scheduler ---------------------------------------- */

/* Appelé dans le contexte de la tâche Zigbee — ne doit pas bloquer.
 * Lit les globaux écrits par sensor_task et envoie les attributs ZCL. */
static void on_zigbee_poll(void)
{
    if (!g_valid) return;
    zigbee_report_temp(g_temp);
    zigbee_report_hum(g_hum);
    zigbee_report_co2(g_eco2);
    zigbee_report_tvoc(g_tvoc);
    zigbee_report_aqi(g_aqi);
}

/* ----- Point d'entrée ---------------------------------------------------- */

void app_main(void)
{
    /* NVS requis par Zigbee pour stocker les credentials réseau */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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

    /* Écran de démarrage */
    ssd1306_draw_rect(&oled, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT);
    ssd1306_draw_string(&oled, 14, 1, "XIAO ESP32-C6");
    ssd1306_draw_string(&oled, 4, 3, "Zigbee joining");
    ssd1306_draw_string(&oled, 28, 5, "please wait");
    ssd1306_flush(&oled);

    /* Tâche de lecture capteurs + affichage OLED */
    xTaskCreate(sensor_task, "sensors", 4096, NULL, 4, NULL);

    /* Démarrage Zigbee — on_zigbee_poll sera appelé à chaque intervalle */
    zigbee_start(on_zigbee_poll);
}
