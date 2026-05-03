idf_component_register(SRCS "ens160.c" "aht21.c" "main.c" "ssd1306.c" "zigbee.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash)
