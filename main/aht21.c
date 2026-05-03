#include "aht21.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aht21";

esp_err_t aht21_init(aht21_t *sensor, i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AHT21_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &sensor->dev));

    vTaskDelay(pdMS_TO_TICKS(40)); /* power-on delay */

    /* if calibration bit not set, send init command */
    uint8_t status = 0;
    i2c_master_receive(sensor->dev, &status, 1, 100);
    if (!(status & 0x08)) {
        uint8_t init_cmd[] = {0xBE, 0x08, 0x00};
        i2c_master_transmit(sensor->dev, init_cmd, sizeof(init_cmd), 100);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "AHT21 initialisé");
    return ESP_OK;
}

esp_err_t aht21_read(aht21_t *sensor, float *temp_c, float *hum_pct)
{
    uint8_t cmd[] = {0xAC, 0x33, 0x00};
    esp_err_t ret = i2c_master_transmit(sensor->dev, cmd, sizeof(cmd), 100);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(80)); /* measurement time */

    uint8_t data[6];
    ret = i2c_master_receive(sensor->dev, data, sizeof(data), 100);
    if (ret != ESP_OK) return ret;

    if (data[0] & 0x80) return ESP_ERR_NOT_FINISHED; /* still busy */

    uint32_t raw_hum  = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *hum_pct = (float)raw_hum  / 1048576.0f * 100.0f;
    *temp_c  = (float)raw_temp / 1048576.0f * 200.0f - 50.0f;

    return ESP_OK;
}
