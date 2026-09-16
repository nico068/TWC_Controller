/*
 * Circular log buffer used to keep recent messages in memory.
 * The buffer is protected by a mutex so producers and readers can safely
 * access it from different tasks.
 */

#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


typedef struct
{
    logger_entry_t entries[LOGGER_BUFFER_SIZE];

    uint16_t head;

    uint16_t tail;

    uint16_t count;

    SemaphoreHandle_t mutex;

} logger_context_t;

static logger_context_t s_logger;

/* Acquires exclusive access to the log buffer. */
static inline void logger_lock(void)
{
    xSemaphoreTake(s_logger.mutex, portMAX_DELAY);
}

/* Releases exclusive access to the log buffer. */
static inline void logger_unlock(void)
{
    xSemaphoreGive(s_logger.mutex);
}

/* Resets the circular log buffer state. */
static void logger_reset(void)
{
    s_logger.head = 0;

    s_logger.tail = 0;

    s_logger.count = 0;

    memset(
        s_logger.entries,
        0,
        sizeof(s_logger.entries));
}

/* Advances a circular buffer index. */
static uint16_t logger_next_index(
    uint16_t index)
{
    index++;

    if (index >= LOGGER_BUFFER_SIZE)
    {
        index = 0;
    }

    return index;
}

void logger_init(void)
{
    memset(&s_logger, 0, sizeof(s_logger));

    s_logger.mutex = xSemaphoreCreateMutex();

    configASSERT(s_logger.mutex != NULL);

    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI("LOGGER", "Logger initialized");
}

uint16_t logger_count(void)
{
    uint16_t value;

    logger_lock();

    value = s_logger.count;

    logger_unlock();

    return value;
}

/* Stores a log entry in the circular buffer. */
static void logger_push(const logger_entry_t *entry)
{
    s_logger.entries[s_logger.head] = *entry;

    s_logger.head = logger_next_index(s_logger.head);

    if (s_logger.count < LOGGER_BUFFER_SIZE)
    {
        s_logger.count++;
    }
    else
    {
        s_logger.tail = logger_next_index(s_logger.tail);
    }
}

/* Read a log entry */
bool logger_get(uint16_t index,
                logger_entry_t *entry)
{
    uint16_t pos;

    if (entry == NULL)
    {
        return false;
    }

    logger_lock();

    if (index >= s_logger.count)
    {
        logger_unlock();
        return false;
    }

    pos = s_logger.tail + index;

    if (pos >= LOGGER_BUFFER_SIZE)
    {
        pos -= LOGGER_BUFFER_SIZE;
    }

    *entry = s_logger.entries[pos];

    logger_unlock();

    return true;
}

/* Returns the ESP-IDF log level matching the logger level. */
static esp_log_level_t logger_to_esp_level(
    logger_level_t level)
{
    switch (level)
    {
        case LOGGER_ERROR:
            return ESP_LOG_ERROR;

        case LOGGER_WARN:
            return ESP_LOG_WARN;

        case LOGGER_INFO:
            return ESP_LOG_INFO;

        case LOGGER_DEBUG:
            return ESP_LOG_DEBUG;

        default:
            return ESP_LOG_INFO;
    }
}

/* Prints a log entry to the ESP-IDF logging backend. */
static void logger_print_entry(
    const logger_entry_t *entry)
{
    esp_log_level_t level =
        logger_to_esp_level(
            entry->level);

    esp_log_write(
        level,
        entry->module,
        "%s\n",
        entry->message);
}

/* Formats, stores and prints a log message. */
void logger_write(logger_level_t level,
                  const char *module,
                  const char *fmt,
                  ...)
{
    logger_entry_t entry;

    va_list args;

    if ((module == NULL) ||
        (fmt == NULL))
    {
        return;
    }

    memset(&entry, 0, sizeof(entry));

    entry.timestamp_us = esp_timer_get_time();

    entry.level = level;

    strlcpy(
        entry.module,
        module,
        sizeof(entry.module));

    va_start(args, fmt);

    vsnprintf(entry.message,
              LOGGER_MAX_MESSAGE_LENGTH,
              fmt,
              args);

    va_end(args);

    logger_lock();

    logger_push(&entry);

    logger_unlock();

    logger_print_entry(&entry);
}
