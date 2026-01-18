idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        nvs_flash
        vfs
        cjson             # JSON parser
)