idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        version
        bootctl
        ble_gatt
        telemetry
        relay_ctrl
        status_led
        pid_controller
        machine_state
)
