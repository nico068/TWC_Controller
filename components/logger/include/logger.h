/*
 * Lightweight circular logger used by the application to keep a bounded
 * history of runtime events and diagnostics.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGGER_MAX_MESSAGE_LENGTH    128
#define LOGGER_MAX_MODULE_LENGTH      16
/* Keep enough recent diagnostics without reserving about 40 KiB of DRAM. */
#define LOGGER_BUFFER_SIZE            64

typedef enum
{
    LOGGER_NONE = 0,
    LOGGER_ERROR,
    LOGGER_WARN,
    LOGGER_INFO,
    LOGGER_DEBUG

} logger_level_t;

typedef struct
{
    uint64_t timestamp_us;

    logger_level_t level;

    char module[LOGGER_MAX_MODULE_LENGTH];

    char message[LOGGER_MAX_MESSAGE_LENGTH];

} logger_entry_t;

/******************************************************************************
 * Initialization
 ******************************************************************************/

void logger_init(void);

/******************************************************************************
 * Logging
 ******************************************************************************/

void logger_write(logger_level_t level,
                  const char *module,
                  const char *fmt,
                  ...);

/******************************************************************************
 * Buffer
 ******************************************************************************/

uint16_t logger_count(void);

/******************************************************************************
 * Buffer access
 ******************************************************************************/

/* Returns one log entry (index 0 = oldest entry). */
bool logger_get(uint16_t index,
                logger_entry_t *entry);

#define logger_info(module, fmt, ...)                  \
    logger_write(                                      \
        LOGGER_INFO,                                   \
        module,                                        \
        fmt,                                           \
        ##__VA_ARGS__)

#define logger_warn(module, fmt, ...)                  \
    logger_write(                                      \
        LOGGER_WARN,                                   \
        module,                                        \
        fmt,                                           \
        ##__VA_ARGS__)

#define logger_error(module, fmt, ...)                 \
    logger_write(                                      \
        LOGGER_ERROR,                                  \
        module,                                        \
        fmt,                                           \
        ##__VA_ARGS__)

#define logger_debug(module, fmt, ...)                 \
    logger_write(                                      \
        LOGGER_DEBUG,                                  \
        module,                                        \
        fmt,                                           \
        ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
