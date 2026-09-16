#include "shelly_task.h"

#include "logger.h"
#include "shelly_device.h"
#include "shelly_measurement.h"
#include "watchdog.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "SHELLY_TASK"

/* Shelly polling period (ms). */
#define SHELLY_TASK_PERIOD_MS    1000

/* Delay between two device-detection attempts while the Shelly is unavailable. */
#define SHELLY_DETECTION_RETRY_MS    2000

/* Maximum time allowed for a graceful task shutdown (ms). */
/* Covers two consecutive 3-second HTTP detection timeouts plus task wake-up. */
#define SHELLY_TASK_STOP_TIMEOUT_MS    10000

/* Shelly task handle. */
static TaskHandle_t s_task_handle;

/* Requests a graceful stop of the Shelly polling task. */
static bool s_stop_requested;

/* Protects concurrent access to the Shelly task control state. */
static SemaphoreHandle_t s_state_mutex;

/* Returns whether the Shelly task has been requested to stop. */
static bool shelly_task_stop_is_requested(void)
{
    bool stop_requested;

    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    stop_requested =
        s_stop_requested;

    xSemaphoreGive(
        s_state_mutex);

    return stop_requested;
}

/* Marks the Shelly polling task as terminated. */
static void shelly_task_mark_stopped(void)
{
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    s_task_handle =
        NULL;

    xSemaphoreGive(
        s_state_mutex);
}

/* Shelly polling task. */
static void shelly_task(
    void *argument);

/* Initializes the Shelly polling task control state. */
esp_err_t shelly_task_init(void)
{
    s_task_handle =
        NULL;

    s_stop_requested =
        false;

    s_state_mutex =
        xSemaphoreCreateMutex();

    if (s_state_mutex == NULL)
    {
        logger_error(
            TAG,
            "Unable to create state mutex");

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/* Starts the Shelly polling task. */
esp_err_t shelly_task_start(void)
{
    if (s_state_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Protect the task state while preparing a new instance. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    if (s_task_handle != NULL)
    {
        xSemaphoreGive(
            s_state_mutex);

        return ESP_OK;
    }

    /* Clear any previous stop request before starting a new task. */
    s_stop_requested =
        false;

    xSemaphoreGive(
        s_state_mutex);

    /*
     * Create the task outside the mutex because it may start immediately
     * and attempt to acquire the Shelly state mutex.
     */
    BaseType_t result =
        xTaskCreate(
            shelly_task,
            "shelly",
            12288,
            NULL,
            5,
            &s_task_handle);

    if (result != pdPASS)
    {
        /* Keep the task state consistent after a creation failure. */
        xSemaphoreTake(
            s_state_mutex,
            portMAX_DELAY);

        s_task_handle =
            NULL;

        xSemaphoreGive(
            s_state_mutex);

        logger_error(
            TAG,
            "Unable to create task");

        return ESP_FAIL;
    }

    logger_info(
        TAG,
        "Shelly task started");

    return ESP_OK;
}

/* Stops the Shelly polling task. */
esp_err_t shelly_task_stop(void)
{
    bool task_running;

    if (s_state_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Protect the task state while requesting the stop. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    task_running =
        (s_task_handle != NULL);

    if (task_running)
    {
        s_stop_requested =
            true;
    }

    xSemaphoreGive(
        s_state_mutex);

    if (!task_running)
    {
        return ESP_OK;
    }

    logger_info(
        TAG,
        "Shelly task stop requested");

    TickType_t start_tick =
        xTaskGetTickCount();

    /* Wait until the Shelly task has terminated itself. */
    for (;;)
    {
        xSemaphoreTake(
            s_state_mutex,
            portMAX_DELAY);

        task_running =
            (s_task_handle != NULL);

        xSemaphoreGive(
            s_state_mutex);

        if (!task_running)
        {
            break;
        }

        if ((xTaskGetTickCount() - start_tick) >=
            pdMS_TO_TICKS(
                SHELLY_TASK_STOP_TIMEOUT_MS))
        {
            logger_error(
                TAG,
                "Shelly task stop timeout");

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                10));
    }

    logger_info(
        TAG,
        "Shelly task stopped");

    return ESP_OK;
}

/* Shelly polling task. */
static void shelly_task(
    void *argument)
{
    (void)argument;

    /* A restarted task must always detect the currently configured host. */
    bool device_detected =
        false;

    /* Register this task in the software watchdog. */
    watchdog_register(
        WD_SHELLY);

    for (;;)
    {
        if (shelly_task_stop_is_requested())
        {
            break;
        }

        watchdog_feed(
            WD_SHELLY);

        if (!device_detected)
        {
            /* Never retain a reading while the configured device is unknown. */
            shelly_measurement_invalidate();

            device_detected =
                shelly_device_detect();

            if (!device_detected)
            {
                /*
                 * WiFi or the Shelly may be temporarily unavailable. Keep the
                 * task alive so recovery does not require an ESP32 reboot.
                 */
                vTaskDelay(
                    pdMS_TO_TICKS(
                        SHELLY_DETECTION_RETRY_MS));

                continue;
            }
        }

        (void)shelly_measurement_update();

        if (shelly_task_stop_is_requested())
        {
            break;
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                SHELLY_TASK_PERIOD_MS));
    }

    watchdog_unregister(
        WD_SHELLY);

    shelly_task_mark_stopped();

    vTaskDelete(
        NULL);
}
