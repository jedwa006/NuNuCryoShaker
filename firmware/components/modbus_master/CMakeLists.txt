idf_component_register(
    SRCS "modbus_master.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)
