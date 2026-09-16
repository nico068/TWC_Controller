#include "power_manager.h"
#include "config.h"
#include "logger.h"
#include "shelly_measurement.h"
#include "watchdog.h"
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "POWER_MANAGER"

/* Power manager task period (ms). */
#define POWER_MANAGER_PERIOD_MS    1000

/* Maximum time allowed for the power manager task to stop cleanly. */
#define POWER_MANAGER_STOP_TIMEOUT_MS 3000

/* Disables charging and invalidates the current power manager state. */
static void power_manager_disable_charge(void);

/* Computes the available charging current from a Shelly measurement snapshot. */
static void power_manager_compute_current(
    const shelly_measurement_t *measurement);

/* Task handle. */
static TaskHandle_t s_task_handle;

/* Indicates whether the power manager task must stop. */
static bool s_stop_requested;

/* Protects concurrent access to the power manager state. */
static SemaphoreHandle_t s_state_mutex;

/* Available charging current in centiamperes (0.01 A). */
static uint16_t s_available_current_ca;

/* Latest house current measured by the Shelly in amperes. */
static float s_house_current_a;

/* Latest house power measured by the Shelly in watts. */
static float s_house_power_w;

/* Indicates whether the latest measurement processed by the power manager is valid. */
static bool s_measurement_valid;

/* Indicates whether the previous Shelly measurement was valid. */
static bool s_previous_measurement_valid;

/* Returns whether the power manager task has been requested to stop. */
static bool power_manager_stop_is_requested(void)
{
    bool stop_requested;

    /* Protect the task control state while reading the stop request. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    stop_requested =
        s_stop_requested;

    xSemaphoreGive(
        s_state_mutex);

    return stop_requested;
}


/* Marks the power manager task as terminated. */
static void power_manager_task_mark_stopped(void)
{
    /* Protect the task control state while clearing the task handle. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    s_task_handle =
        NULL;

    xSemaphoreGive(
        s_state_mutex);
}

/* Power manager task. */
static void power_manager_task(
    void *argument);

/* Initializes the power manager. */
esp_err_t power_manager_init(void)
{
    /* No stop request is pending at startup. */
    s_stop_requested =
        false;

    /* Reset the power manager state. */
    s_available_current_ca =
        0;

    s_house_current_a =
        0.0f;

    s_house_power_w =
        0.0f;

    /* No valid measurement has been processed at startup. */
    s_measurement_valid =
        false;

    /* No valid Shelly measurement is known at startup. */
    s_previous_measurement_valid =
        false;

    s_task_handle =
        NULL;

    /* Create the mutex protecting the shared power manager state. */
    s_state_mutex =
        xSemaphoreCreateMutex();

    if (s_state_mutex == NULL)
    {
        logger_error(
            TAG,
            "Unable to create state mutex");

        return ESP_ERR_NO_MEM;
    }

    logger_info(
        TAG,
        "Power manager initialized");

    return ESP_OK;
}

/* Returns a consistent snapshot of the current power manager status. */
bool power_manager_get_status(
    power_manager_status_t *status)
{
    /* Reject invalid output pointers. */
    if (status == NULL)
    {
        return false;
    }

    /* Protect the shared state while copying the power manager snapshot. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    status->measurement_valid =
        s_measurement_valid;

    status->house_current_a =
        s_house_current_a;

    status->house_power_w =
        s_house_power_w;

    status->available_current_ca =
        s_available_current_ca;

    /* Expose the computed headroom consistently to every status endpoint. */
    status->charge_limit_ca =
        s_available_current_ca;

    status->charging_enabled =
        (s_available_current_ca > 0U);

    xSemaphoreGive(
        s_state_mutex);

    return true;
}

/* Disables charging and invalidates the current power manager state. */
static void power_manager_disable_charge(void)
{
    /* Protect the shared state while disabling power management output. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    /* No charging current is available while charging is disabled. */
    s_available_current_ca =
        0;

    /* The power manager no longer exposes a valid active measurement. */
    s_measurement_valid =
        false;

    xSemaphoreGive(
        s_state_mutex);

}

/* Immediately invalidates the published power-management result. */
void power_manager_invalidate(void)
{
    power_manager_disable_charge();
}
/* Computes the available charging current from a Shelly measurement snapshot. */
static void power_manager_compute_current(
    const shelly_measurement_t *measurement)
{
    const system_config_t *cfg =
        config_get();

    /* Reject invalid measurement pointers. */
    if (measurement == NULL)
    {
        power_manager_disable_charge();

        return;
    }

    /* Installation breaker limit in amperes. */
    float installation_current_a =
        (float)cfg->breaker_limit;

    /* Minimum charger current in amperes. */
    float min_charge_current_a =
        (float)cfg->charger_min_current;

    /* Maximum charger current in amperes. */
    float max_charge_current_a =
        (float)cfg->charger_max_current;

    /* Safety margin in amperes. */
    const float safety_margin_a =
        2.0f;

    /* Read the house current from the consistent Shelly snapshot. */
    float house_current_a =
        measurement->current;

    /* Disable charging when the house current cannot be trusted. */
    if (!isfinite(house_current_a) ||
        (house_current_a < 0.0f))
    {
        logger_warn(
            TAG,
            "Invalid Shelly house current");

        power_manager_disable_charge();

        return;
    }

    /* Read the house power from the same Shelly snapshot. */
    float house_power_w =
        measurement->power;

    /* Preserve the last known house power when the new value is invalid. */
    if (!isfinite(house_power_w))
    {
        xSemaphoreTake(
            s_state_mutex,
            portMAX_DELAY);

        house_power_w =
            s_house_power_w;

        xSemaphoreGive(
            s_state_mutex);

        logger_warn(
            TAG,
            "Invalid Shelly house power");
    }

    /* Compute the charging current still available in amperes. */
    float available_current_a =
        installation_current_a -
        house_current_a -
        safety_margin_a;

    /* Prevent a negative charging current. */
    if (available_current_a < 0.0f)
    {
        available_current_a =
            0.0f;
    }

    /* Enforce the configured maximum charger current. */
    if (available_current_a >
        max_charge_current_a)
    {
        available_current_a =
            max_charge_current_a;
    }

    /* Disable charging below the configured minimum current. */
    if (available_current_a <
        min_charge_current_a)
    {
        available_current_a =
            0.0f;
    }

    /* Convert the available current from amperes to centiamperes. */
    uint16_t available_current_ca =
        (uint16_t)(
            available_current_a *
            100.0f);

    /* Publish a consistent snapshot of the latest power manager state. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    s_house_current_a =
        house_current_a;

    s_house_power_w =
        house_power_w;

    s_available_current_ca =
        available_current_ca;

    /* Mark the published measurement as valid only with its computed values. */
    s_measurement_valid =
        true;

    xSemaphoreGive(
        s_state_mutex);

    logger_debug(
        TAG,
        "House %.2f A -> limit %.2f A",
        house_current_a,
        available_current_ca / 100.0f);
}

/* Starts the power manager. */
esp_err_t power_manager_start(void)
{
    /* Protect the task control state while preparing a new task instance. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    /* Do not create another task when the power manager is already running. */
    if (s_task_handle != NULL)
    {
        xSemaphoreGive(
            s_state_mutex);

        return ESP_OK;
    }

    /* Reset task state before starting a new power manager instance. */
    s_previous_measurement_valid =
        false;

    s_stop_requested =
        false;

    xSemaphoreGive(
        s_state_mutex);

    /*
     * Task creation is intentionally performed outside the mutex because the
     * new task may immediately attempt to acquire the same state mutex.
     */
    BaseType_t result =
        xTaskCreate(
            power_manager_task,
            "power_manager",
            4096,
            NULL,
            5,
            &s_task_handle);

    if (result != pdPASS)
    {
        logger_error(
            TAG,
            "Unable to create task");

        return ESP_FAIL;
    }

    logger_info(
        TAG,
        "Power manager started");

    return ESP_OK;
}

/* Stops the power manager cooperatively and disables charging. */
esp_err_t power_manager_stop(void)
{
    TickType_t start_tick =
        xTaskGetTickCount();

    /* Request a cooperative stop when the task is currently running. */
    xSemaphoreTake(
        s_state_mutex,
        portMAX_DELAY);

    bool task_running =
        (s_task_handle != NULL);

    if (task_running)
    {
        s_stop_requested =
            true;
    }

    xSemaphoreGive(
        s_state_mutex);

    /* Wait until the power manager task has terminated itself. */
    while (task_running)
    {
        /* Protect the task control state while checking the task handle. */
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

        /* Abort the wait if the task does not stop within the expected time. */
        if ((xTaskGetTickCount() - start_tick) >=
            pdMS_TO_TICKS(
                POWER_MANAGER_STOP_TIMEOUT_MS))
        {
            /* Apply a safe zero charging limit even if the task did not stop. */
            power_manager_disable_charge();

            logger_error(
                TAG,
                "Power manager task stop timeout");

            return ESP_ERR_TIMEOUT;
        }

        /* Allow the power manager task to reach its cooperative stop point. */
        vTaskDelay(
            pdMS_TO_TICKS(
                10));
    }

    /* Clear the available current and apply a safe zero charging limit. */
    power_manager_disable_charge();

    logger_info(
        TAG,
        "Power manager stopped");

    return ESP_OK;
}

/* Runs the power manager and updates the charging current limit. */
static void power_manager_task(
    void *argument)
{
    (void)argument;

    /* Register the power manager task in the software watchdog. */
    watchdog_register(
        WD_ENERGY);

    for (;;)
    {
        shelly_measurement_t measurement;

        /* Stop the task only between two power management cycles. */
        if (power_manager_stop_is_requested())
        {
            break;
        }

        /* Refresh the power manager software watchdog. */
        watchdog_feed(
            WD_ENERGY);

        /* Read one consistent snapshot of the latest Shelly measurement. */
        bool measurement_valid =
            shelly_measurement_get(
                &measurement);    

        if (measurement_valid)
        {
            /* Report recovery only when the measurement becomes valid again. */
            if (!s_previous_measurement_valid)
            {
                logger_info(
                    TAG,
                    "Shelly measurement is valid");
            }

            /* Compute the charging limit from the same measurement snapshot. */
            power_manager_compute_current(
                &measurement);
        }
        else
        {
            /* Disable charging when no valid measurement is available. */
            power_manager_disable_charge();

            /* Report the failure only when the measurement becomes invalid. */
            if (s_previous_measurement_valid)
            {
                logger_warn(
                    TAG,
                    "Shelly measurement is not valid");
            }
        }

        /* Remember the measurement state for transition detection. */
        s_previous_measurement_valid =
            measurement_valid;

        /* Stop immediately after the current power management cycle completes. */
        if (power_manager_stop_is_requested())
        {
            break;
        }

        /* Wait before updating the power manager again. */
        vTaskDelay(
            pdMS_TO_TICKS(
                POWER_MANAGER_PERIOD_MS));
    }

    /* Stop monitoring this task before it terminates. */
    watchdog_unregister(
        WD_ENERGY);

    /* Publish that the task has terminated before deleting itself. */
    power_manager_task_mark_stopped();  

    vTaskDelete(
        NULL);
}
