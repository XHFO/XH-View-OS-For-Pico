idf_component_register(
    SRCS "../../main/updater_main.c"
    REQUIRES read_pico app_update fatfs mbedtls esp_partition
    INCLUDE_DIRS ".")
