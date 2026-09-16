#include "watchdog.h"

#include "logger.h"

#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#define WATCHDOG_TIMEOUT_MS    5000

typedef struct
{
    bool registered;

    uint64_t last_feed;

    bool last_alive_known;

    bool last_alive;

    uint32_t timeout_count;

    uint32_t recovery_count;

} watchdog_entry_t;

static watchdog_entry_t s_watchdog[WD_MAX_TASKS];

/* Protects concurrent access to the watchdog task table. */
static SemaphoreHandle_t s_watchdog_mutex;

/* Checks whether a watchdog task identifier is within the valid range. */
static bool watchdog_task_is_valid(
    watchdog_task_t task)
{
    return (task >= 0) &&
           (task < WD_MAX_TASKS);
}

/* Initializes the software watchdog state and synchronization resources. */
void watchdog_init(void)
{
    /* Clear all watchdog task states and cumulative counters. */
    memset(
        s_watchdog,
        0,
        sizeof(s_watchdog));

    /* Create the mutex protecting the watchdog task table. */
    s_watchdog_mutex =
        xSemaphoreCreateMutex();

    configASSERT(
        s_watchdog_mutex != NULL);

    logger_write(
        LOGGER_INFO,
        "WATCHDOG",
        "Initialized");
}

/* Registers a task with the software watchdog. */
void watchdog_register(watchdog_task_t task)
{
    /* Reject invalid watchdog identifiers before accessing the task table. */
    if (!watchdog_task_is_valid(task))
    {
        return;
    }

    /* Protect the watchdog task state while it is being initialized. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    s_watchdog[task].registered =
        true;

    s_watchdog[task].last_feed =
        esp_timer_get_time();

    s_watchdog[task].last_alive_known =
        true;

    s_watchdog[task].last_alive =
        true;

    xSemaphoreGive(
        s_watchdog_mutex);

    logger_write(
        LOGGER_INFO,
        "WATCHDOG",
        "Task %d registered",
        task);
}

/* Unregisters a task from the software watchdog. */
void watchdog_unregister(watchdog_task_t task)
{
    /* Reject invalid watchdog identifiers before accessing the task table. */
    if (!watchdog_task_is_valid(task))
    {
        return;
    }

    /* Protect the watchdog task state while it is being disabled. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    s_watchdog[task].registered =
        false;

    s_watchdog[task].last_alive_known =
        false;

    s_watchdog[task].last_alive =
        false;

    xSemaphoreGive(
        s_watchdog_mutex);

    logger_write(
        LOGGER_INFO,
        "WATCHDOG",
        "Task %d unregistered",
        task);
}

/* Refreshes the watchdog timestamp for a registered task. */
void watchdog_feed(watchdog_task_t task)
{
    /* Reject invalid watchdog identifiers before accessing the task table. */
    if (!watchdog_task_is_valid(task))
    {
        return;
    }

    /* Protect the watchdog task state while refreshing its timestamp. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    /* Ignore feed requests for tasks that are not currently monitored. */
    if (!s_watchdog[task].registered)
    {
        xSemaphoreGive(
            s_watchdog_mutex);

        return;
    }

    /* Record the current timestamp as the latest watchdog feed. */
    s_watchdog[task].last_feed =
        esp_timer_get_time();

    xSemaphoreGive(
        s_watchdog_mutex);
}

/* Checks whether a watchdog task is still alive. */
bool watchdog_is_alive(watchdog_task_t task)
{
    uint64_t now;
    bool alive;

    /* Reject invalid watchdog identifiers before accessing the task table. */
    if (!watchdog_task_is_valid(task))
    {
        return false;
    }

    /* Protect the watchdog task state while checking its timestamp. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    /* Unregistered tasks are not considered alive. */
    if (!s_watchdog[task].registered)
    {
        xSemaphoreGive(
            s_watchdog_mutex);

        return false;
    }

    /* Compare the last feed timestamp against the watchdog timeout. */
    now =
        esp_timer_get_time();

    alive =
        ((now - s_watchdog[task].last_feed)
         < (WATCHDOG_TIMEOUT_MS * 1000ULL));

    xSemaphoreGive(
        s_watchdog_mutex);

    return alive;
}

/* Checks whether a task is registered with the software watchdog. */
bool watchdog_is_registered(watchdog_task_t task)
{
    bool registered;

    /* Reject invalid watchdog identifiers before accessing the task table. */
    if (!watchdog_task_is_valid(task))
    {
        return false;
    }

    /* Protect the watchdog task state while reading its registration state. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    registered =
        s_watchdog[task].registered;

    xSemaphoreGive(
        s_watchdog_mutex);

    return registered;
}

/* Records the current health state and detects watchdog state transitions. */
watchdog_transition_t watchdog_note_health(
    watchdog_task_t task,
    bool is_alive)
{
    watchdog_entry_t *entry;
    watchdog_transition_t transition =
        WATCHDOG_TRANSITION_NONE;

    /* Reject invalid watchdog identifiers before accessing the task table. */
    if (!watchdog_task_is_valid(task))
    {
        return WATCHDOG_TRANSITION_NONE;
    }

    /* Protect watchdog health state while checking and updating transitions. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    entry =
        &s_watchdog[task];

    /* Ignore tasks that are not currently monitored. */
    if (!entry->registered)
    {
        xSemaphoreGive(
            s_watchdog_mutex);

        return WATCHDOG_TRANSITION_NONE;
    }

    /* Initialize health tracking when no previous state is available. */
    if (!entry->last_alive_known)
    {
        entry->last_alive_known =
            true;

        entry->last_alive =
            is_alive;

        xSemaphoreGive(
            s_watchdog_mutex);

        return WATCHDOG_TRANSITION_NONE;
    }

    /* Ignore repeated observations of the same health state. */
    if (entry->last_alive == is_alive)
    {
        xSemaphoreGive(
            s_watchdog_mutex);

        return WATCHDOG_TRANSITION_NONE;
    }

    /* Record the new health state before reporting the transition. */
    entry->last_alive =
        is_alive;

    if (!is_alive)
    {
        /* Count a transition from alive to timed out. */
        entry->timeout_count++;

        transition =
            WATCHDOG_TRANSITION_TIMEOUT;
    }
    else
    {
        /* Count a transition from timed out to recovered. */
        entry->recovery_count++;

        transition =
            WATCHDOG_TRANSITION_RECOVERED;
    }

    xSemaphoreGive(
        s_watchdog_mutex);

    return transition;
}

/* Returns a consistent snapshot of a watchdog task status. */
bool watchdog_get_status(
    watchdog_task_t task,
    watchdog_status_t *status)
{
    watchdog_entry_t *entry;
    uint64_t now;

    /* Reject invalid arguments before accessing the task table. */
    if (!watchdog_task_is_valid(task) ||
        (status == NULL))
    {
        return false;
    }

    /* Protect all status fields so they belong to the same snapshot. */
    xSemaphoreTake(
        s_watchdog_mutex,
        portMAX_DELAY);

    entry =
        &s_watchdog[task];

    /* Copy the registration state before evaluating task health. */
    status->registered =
        entry->registered;

    /* Unregistered tasks are not considered alive. */
    if (!entry->registered)
    {
        status->alive =
            false;
    }
    else
    {
        /* Evaluate health directly to avoid recursively taking the mutex. */
        now =
            esp_timer_get_time();

        status->alive =
            ((now - entry->last_feed)
             < (WATCHDOG_TIMEOUT_MS * 1000ULL));
    }

    /* Export the latest watchdog timing and cumulative transition counters. */
    status->last_feed_ms =
        entry->last_feed / 1000ULL;

    status->timeout_count =
        entry->timeout_count;

    status->recovery_count =
        entry->recovery_count;

    xSemaphoreGive(
        s_watchdog_mutex);

    return true;
}

/* Returns the human-readable name of a watchdog task. */
const char *watchdog_task_name(watchdog_task_t task)
{
    switch (task)
    {
        case WD_SUPERVISOR:
            return "supervisor";

        case WD_SHELLY:
            return "shelly";

        case WD_ENERGY:
            return "energy";

        case WD_RS485:
            return "rs485";

        case WD_OTA:
            return "ota";

        default:
            return "unknown";
    }
}