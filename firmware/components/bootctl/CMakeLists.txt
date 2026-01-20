idf_component_register(
    SRCS "bootctl.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        nvs_flash
        app_update
        esp_partition
        esp_driver_gpio
)