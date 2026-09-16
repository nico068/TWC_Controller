idf_component_register(
    SRCS
        "wifi_manager.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        config
        logger
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
        esp_hw_support
)
