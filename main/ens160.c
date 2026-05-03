#include <string.h>
#include "ens160.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ens160";

#define REG_OPMODE   0x10
#define REG_TEMP_IN  0x13
#define REG_RH_IN    0x15
#define REG_DATA_AQI 0x21

static esp_err_t reg_write(ens160_t *s, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[8];
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s->dev, buf, len + 1, 100);
}

static esp_err_t reg_read(ens160_t *s, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s->dev, &reg, 1, data, len, 100);
}

esp_err_t ens160_init(ens160_t *sensor, i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ENS160_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &sensor->dev));

    /* reset then switch to standard operating mode */
    uint8_t mode = 0x10;
    reg_write(sensor, REG_OPMODE, &mode, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    mode = 0x02;
    reg_write(sensor, REG_OPMODE, &mode, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "ENS160 initialisé");
    return ESP_OK;
}

esp_err_t ens160_set_compensation(ens160_t *sensor, float temp_c, float hum_pct)
{
    uint16_t t = (uint16_t)((temp_c + 273.15f) * 64.0f);
    uint16_t h = (uint16_t)(hum_pct * 512.0f);

    uint8_t buf[2];
    buf[0] = t & 0xFF; buf[1] = t >> 8;
    reg_write(sensor, REG_TEMP_IN, buf, 2);
    buf[0] = h & 0xFF; buf[1] = h >> 8;
    reg_write(sensor, REG_RH_IN, buf, 2);

    return ESP_OK;
}

esp_err_t ens160_read(ens160_t *sensor, ens160_data_t *data)
{
    uint8_t raw[5];
    esp_err_t ret = reg_read(sensor, REG_DATA_AQI, raw, sizeof(raw));
    if (ret != ESP_OK) return ret;

    data->aqi  = raw[0];
    data->tvoc = (uint16_t)(raw[1] | ((uint16_t)raw[2] << 8));
    data->eco2 = (uint16_t)(raw[3] | ((uint16_t)raw[4] << 8));

    return ESP_OK;
}
