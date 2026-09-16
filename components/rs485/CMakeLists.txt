idf_component_register(
    SRCS
        "rs485.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        config
        driver
        logger
        shelly
        watchdog
)
