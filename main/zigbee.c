#include "zigbee.h"
#include "esp_zigbee_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "zigbee";

#define ENDPOINT     1
#define UPDATE_MS    30000
#define CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

/* Cluster manufacturer-specific pour TVOC + AQI.
 * 0x042E (TVOC ZCL standard) est connu de ZBOSS en binaire → crash garanti.
 * La plage 0xFC00–0xFFFF est manufacturer-specific : ZBOSS ne l'a jamais en table interne. */
#define ZB_CLUSTER_AIR   0xFC00  /* cluster custom TVOC+AQI */
#define ZB_ATTR_TVOC     0x0000  /* TVOC en ppb (uint16) */
#define ZB_ATTR_AQI      0x0001  /* AQI index 1-5 (uint8) */


static sensor_poll_fn s_poll_fn = NULL;

/* ----- Scheduler Zigbee -------------------------------------------------- */

static void sensor_alarm(uint8_t param)
{
    if (s_poll_fn) s_poll_fn();
    esp_zb_scheduler_alarm(sensor_alarm, 0, UPDATE_MS);
}

/* ----- Handler de signaux réseau (obligatoire) --------------------------- */

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p   = signal_struct->p_app_signal;
    esp_err_t err = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig = (esp_zb_app_signal_type_t)*p;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Nouveau dispositif — recherche réseau Zigbee...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Reconnexion réseau — démarrage mesures");
                esp_zb_scheduler_alarm(sensor_alarm, 0, 1000);
            }
        } else {
            ESP_LOGW(TAG, "Init échouée (0x%x), nouvelle tentative", err);
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Connecté au réseau Zigbee! Adresse: 0x%04X",
                     esp_zb_get_short_address());
            esp_zb_scheduler_alarm(sensor_alarm, 0, 1000); /* première mesure dans 1s */
        } else {
            ESP_LOGI(TAG, "Pas de réseau trouvé, nouvelle tentative...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    default:
        ESP_LOGD(TAG, "Signal: 0x%x err: %s", sig, esp_err_to_name(err));
        break;
    }
}

/* ----- Création de l'endpoint capteur ------------------------------------ */

static esp_zb_ep_list_t *create_sensor_ep(void)
{
    /* Basic cluster */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x01, /* secteur */
    };
    esp_zb_attribute_list_t *basic_attrs = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)"\x09""Espressif");
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,  (void *)"\x0C""XIAO-C6-AirQ");

    /* Identify cluster */
    esp_zb_identify_cluster_cfg_t identify_cfg = {.identify_time = 0};

    /* Temperature cluster — valeur en 0.01 °C (int16) */
    esp_zb_temperature_meas_cluster_cfg_t temp_cfg = {
        .measured_value = 0x8000, /* invalide jusqu'à la 1ère lecture */
        .min_value      = -5000,  /* -50.00 °C */
        .max_value      =  8500,  /* +85.00 °C */
    };

    /* Humidity cluster — valeur en 0.01 % (uint16) */
    esp_zb_humidity_meas_cluster_cfg_t hum_cfg = {
        .measured_value = 0xFFFF,
        .min_value      = 0,
        .max_value      = 10000,
    };

    /* CO2 cluster (0x040D) — helper SDK obligatoire car ZBOSS a une définition interne */
    esp_zb_carbon_dioxide_measurement_cluster_cfg_t co2_cfg = {
        .measured_value     = NAN,
        .min_measured_value = NAN,
        .max_measured_value = NAN,
    };
    esp_zb_attribute_list_t *co2_attrs =
        esp_zb_carbon_dioxide_measurement_cluster_create(&co2_cfg);

    /* Cluster custom TVOC + AQI (0xFC00 manufacturer-specific).
     * Les valeurs initiales DOIVENT être statiques : ZBOSS stocke les pointeurs. */
    static uint16_t s_tvoc_val = 0xFFFF; /* invalid */
    static uint8_t  s_aqi_val  = 0;
    esp_zb_attribute_list_t *air_attrs = esp_zb_zcl_attr_list_create(ZB_CLUSTER_AIR);
    esp_zb_cluster_add_attr(air_attrs, ZB_CLUSTER_AIR, ZB_ATTR_TVOC,
        ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &s_tvoc_val);
    esp_zb_cluster_add_attr(air_attrs, ZB_CLUSTER_AIR, ZB_ATTR_AQI,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        &s_aqi_val);

    /* Assemblage */
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(clusters, basic_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(clusters,
        esp_zb_identify_cluster_create(&identify_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_temperature_meas_cluster(clusters,
        esp_zb_temperature_meas_cluster_create(&temp_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_humidity_meas_cluster(clusters,
        esp_zb_humidity_meas_cluster_create(&hum_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_carbon_dioxide_measurement_cluster(clusters, co2_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_custom_cluster(clusters, air_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg);
    return ep_list;
}

/* ----- Tâche Zigbee (bloquante) ----------------------------------------- */

static void zb_task(void *pv)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg     = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 4000,
        },
    };
    esp_zb_init(&cfg);
    esp_zb_set_primary_network_channel_set(CHANNEL_MASK);
    esp_zb_device_register(create_sensor_ep());
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop(); /* ne retourne jamais */
    vTaskDelete(NULL);
}

/* ----- API publique ------------------------------------------------------- */

void zigbee_start(sensor_poll_fn poll_fn)
{
    s_poll_fn = poll_fn;
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = { .radio_mode = ZB_RADIO_MODE_NATIVE },
        .host_config  = { .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));
    xTaskCreate(zb_task, "zb_main", 4096, NULL, 5, NULL);
}

void zigbee_report_temp(float celsius)
{
    int16_t val = (int16_t)(celsius * 100.0f);
    esp_zb_zcl_set_attribute_val(ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &val, false);
}

void zigbee_report_hum(float percent)
{
    uint16_t val = (uint16_t)(percent * 100.0f);
    esp_zb_zcl_set_attribute_val(ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &val, false);
}

void zigbee_report_co2(uint16_t ppm)
{
    float val = ppm * 1e-6f; /* ppm → fraction molaire mol/mol */
    esp_zb_zcl_set_attribute_val(ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID,
        &val, false);
}

void zigbee_report_tvoc(uint16_t ppb)
{
    esp_zb_zcl_set_attribute_val(ENDPOINT,
        ZB_CLUSTER_AIR, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ATTR_TVOC, &ppb, false);
}

void zigbee_report_aqi(uint8_t aqi)
{
    esp_zb_zcl_set_attribute_val(ENDPOINT,
        ZB_CLUSTER_AIR, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ATTR_AQI, &aqi, false);
}

