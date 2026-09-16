#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    WD_SUPERVISOR = 0,
    WD_SHELLY,
    WD_ENERGY,
    WD_RS485,
    WD_OTA,
    WD_MAX_TASKS

} watchdog_task_t;

typedef enum
{
    WATCHDOG_TRANSITION_NONE = 0,
    WATCHDOG_TRANSITION_TIMEOUT,
    WATCHDOG_TRANSITION_RECOVERED

} watchdog_transition_t;

typedef struct
{
    bool registered;

    bool alive;

    uint64_t last_feed_ms;

    uint32_t timeout_count;

    uint32_t recovery_count;

} watchdog_status_t;

void watchdog_init(void);

void watchdog_register(watchdog_task_t task);

/* Unregisters a task from the software watchdog. */
void watchdog_unregister(watchdog_task_t task);

void watchdog_feed(watchdog_task_t task);

bool watchdog_is_alive(watchdog_task_t task);

bool watchdog_is_registered(watchdog_task_t task);

watchdog_transition_t watchdog_note_health(
    watchdog_task_t task,
    bool is_alive);

bool watchdog_get_status(
    watchdog_task_t task,
    watchdog_status_t *status);

const char *watchdog_task_name(watchdog_task_t task);

#endif