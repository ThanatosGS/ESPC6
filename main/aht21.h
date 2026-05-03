#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define AHT21_ADDR 0x38

typedef struct {
    i2c_master_dev_handle_t dev;
} aht21_t;

esp_err_t aht21_init(aht21_t *sensor, i2c_master_bus_handle_t bus);
esp_err_t aht21_read(aht21_t *sensor, float *temp_c, float *hum_pct);
