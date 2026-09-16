#include "shelly.h"

#include "logger.h"
#include "shelly_measurement.h"
#include "shelly_task.h"

#define TAG "SHELLY"

/* Initializes the Shelly component. */
esp_err_t shelly_init(void)
{
    if (!shelly_device_init())
    {
        return ESP_FAIL;
    }

    if (!shelly_measurement_init())
    {
        return ESP_FAIL;
    }

    if (shelly_task_init() != ESP_OK)
    {
        return ESP_FAIL;
    }

    logger_info(
        TAG,
        "Shelly initialized");

    return ESP_OK;
}

/* Starts the Shelly component. */
esp_err_t shelly_start(void)
{
    /*
     * Start polling even when WiFi is not ready yet. The polling task performs
     * device detection and retries it until the configured Shelly is reachable.
     */
    if (shelly_task_start() != ESP_OK)
    {
        return ESP_FAIL;
    }

    logger_info(
        TAG,
        "Shelly started");

    return ESP_OK;
}

/* Stops the Shelly component. */
esp_err_t shelly_stop(void)
{
    esp_err_t err =
        shelly_task_stop();

    if (err != ESP_OK)
    {
        /* Even a failed stop cannot authorize an old measurement. */
        shelly_measurement_invalidate();

        /* Propagate task shutdown failures to the caller. */
        logger_error(
            TAG,
            "Unable to stop Shelly task (%s)",
            esp_err_to_name(err));

        return err;
    }

    /* A stopped meter must not leave its old reading available to charging. */
    shelly_measurement_invalidate();

    logger_info(
        TAG,
        "Shelly stopped");

    return ESP_OK;
}
