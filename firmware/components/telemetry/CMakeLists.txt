idf_component_register(
    SRCS "telemetry.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_timer
        wire_protocol
        ble_gatt
        session_mgr
)
