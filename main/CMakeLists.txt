idf_component_register(
    SRCS
        "app_main.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        logger
        storage
        config
        watchdog
        power_manager
        rs485
        wifi_manager
        shelly
        webserver
        scheduler
        esp_event
        esp_netif
        spiffs
        app_update
)

# Génération et flash automatique du dossier www vers la partition "spiffs"
spiffs_create_partition_image(spiffs ../www FLASH_IN_PROJECT)
