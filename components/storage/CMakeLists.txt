idf_component_register(
    SRCS
        "storage.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        logger
        nvs_flash
)
