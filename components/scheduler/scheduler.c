#include "scheduler.h"

#include "logger.h"
#include "watchdog.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SCHEDULER";

#define SCHEDULER_TASK_STACK       4096
#define SCHEDULER_TASK_PRIORITY    5
#define SCHEDULER_PERIOD_MS        1000

typedef struct
{
    TaskHandle_t supervisor_task;

} scheduler_context_t;

static scheduler_context_t s_scheduler;

/* Checks one registered watchdog task and reports health transitions. */
static void scheduler_watchdog_check_task(
    watchdog_task_t task)
{
    /* Ignore tasks that are not currently monitored. */
    if (!watchdog_is_registered(task))
    {
        return;
    }

    /* Check whether the task has refreshed its watchdog within the timeout. */
    bool is_alive =
        watchdog_is_alive(task);

    /* Record the health state and detect a possible state transition. */
    watchdog_transition_t transition =
        watchdog_note_health(
            task,
            is_alive);

    /* Report a transition from alive to timed out. */
    if (transition == WATCHDOG_TRANSITION_TIMEOUT)
    {
        logger_warn(
            TAG,
            "Watchdog timeout detected: %s",
            watchdog_task_name(task));
    }
    /* Report a transition from timed out to recovered. */
    else if (transition == WATCHDOG_TRANSITION_RECOVERED)
    {
        logger_info(
            TAG,
            "Watchdog recovered: %s",
            watchdog_task_name(task));
    }
}

/* Checks all watchdog tasks monitored by the scheduler supervisor. */
static void scheduler_watchdog_check_all(void)
{
    for (watchdog_task_t task = WD_SHELLY;
         task < WD_MAX_TASKS;
         task++)
    {
        scheduler_watchdog_check_task(
            task);
    }
}

/* Runs the scheduler supervisor and monitors registered watchdog tasks. */
static void supervisor_task(
    void *pvParameters)
{
    (void)pvParameters;

    /* Register the supervisor task in the software watchdog. */
    watchdog_register(
        WD_SUPERVISOR);

    while (true)
    {
        /* Refresh the supervisor watchdog before checking monitored tasks. */
        watchdog_feed(
            WD_SUPERVISOR);

        /* Check all watchdog tasks managed by the supervisor. */
        scheduler_watchdog_check_all();

        logger_debug(
            TAG,
            "Supervisor alive");

        /* Run the supervisor once per second. */
        vTaskDelay(
            pdMS_TO_TICKS(
                SCHEDULER_PERIOD_MS));
    }
}

/* Initializes the scheduler component. */
esp_err_t scheduler_init(void)
{
    /* No runtime resources are required before the scheduler is started. */
    logger_info(
        TAG,
        "Scheduler initialized");

    return ESP_OK;
}

/* Starts the scheduler supervisor task. */
esp_err_t scheduler_start(void)
{
    /* Do not create another supervisor when the scheduler is already running. */
    if (s_scheduler.supervisor_task != NULL)
    {
        return ESP_OK;
    }

    /* Create the supervisor task responsible for watchdog monitoring. */
    BaseType_t result =
        xTaskCreatePinnedToCore(
            supervisor_task,
            "supervisor",
            SCHEDULER_TASK_STACK,
            NULL,
            SCHEDULER_TASK_PRIORITY,
            &s_scheduler.supervisor_task,
            tskNO_AFFINITY);

    if (result != pdPASS)
    {
        logger_error(
            TAG,
            "Unable to create supervisor task");

        return ESP_FAIL;
    }

    logger_info(
        TAG,
        "Supervisor task started");

    return ESP_OK;
}