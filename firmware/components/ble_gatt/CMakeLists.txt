idf_component_register(
    SRCS "ble_gatt.c"
    INCLUDE_DIRS "include"
    REQUIRES version
    PRIV_REQUIRES
        bt
        nvs_flash
        wire_protocol
        session_mgr
        telemetry
        relay_ctrl
        status_led
        machine_state
        pid_controller
)
