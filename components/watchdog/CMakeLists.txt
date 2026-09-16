idf_component_register(
    SRCS
        "watchdog.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        logger
        esp_timer
)
