idf_component_register(
    SRCS "main.c" "gps.c" "bluetooth.c" "navigation.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_uart bt nvs_flash
)