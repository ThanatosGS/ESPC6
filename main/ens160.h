#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define ENS160_ADDR 0x53

typedef struct {
    i2c_master_dev_handle_t dev;
} ens160_t;

typedef struct {
    uint8_t  aqi;   /* 1=Excellent 2=Good 3=Moderate 4=Poor 5=Unhealthy */
    uint16_t tvoc;  /* ppb */
    uint16_t eco2;  /* ppm */
} ens160_data_t;

esp_err_t ens160_init(ens160_t *sensor, i2c_master_bus_handle_t bus);
esp_err_t ens160_set_compensation(ens160_t *sensor, float temp_c, float hum_pct);
esp_err_t ens160_read(ens160_t *sensor, ens160_data_t *data);
