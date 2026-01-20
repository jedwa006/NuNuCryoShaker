idf_component_register(
    SRCS "machine_state.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_timer
        driver
        relay_ctrl
        session_mgr
        telemetry
        wire_protocol
        pid_controller
        ble_gatt
)
