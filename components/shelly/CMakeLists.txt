idf_component_register(
    SRCS
        "shelly.c"
        "shelly_config.c"
        "shelly_device.c"
        "shelly_http.c"
        "shelly_json.c"
        "shelly_measurement.c"        
        "shelly_task.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        logger
        config
        esp_http_client
        json
        watchdog
)
