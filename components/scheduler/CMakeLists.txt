idf_component_register(
    SRCS
        "scheduler.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        logger
        freertos
        watchdog
)
