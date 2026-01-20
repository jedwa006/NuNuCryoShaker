idf_component_register(
    SRCS "pid_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES modbus_master freertos esp_timer
    PRIV_REQUIRES nvs_flash
)
