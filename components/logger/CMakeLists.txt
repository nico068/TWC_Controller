idf_component_register(
    SRCS
        "logger.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        log
        esp_timer
)