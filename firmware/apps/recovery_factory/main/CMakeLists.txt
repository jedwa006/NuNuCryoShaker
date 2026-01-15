idf_component_register(
    SRCS "ota_portal.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_http_server
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        app_update
        mbedtls
)
