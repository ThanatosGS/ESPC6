#pragma once
#include <stdint.h>

/* Callback appelé par le scheduler Zigbee à chaque intervalle de mesure.
 * Doit lire les capteurs et appeler zigbee_report_*. */
typedef void (*sensor_poll_fn)(void);

void zigbee_start(sensor_poll_fn poll_fn);

void zigbee_report_temp(float celsius);
void zigbee_report_hum(float percent);
void zigbee_report_co2(uint16_t ppm);
void zigbee_report_tvoc(uint16_t ppb);
void zigbee_report_aqi(uint8_t aqi);
