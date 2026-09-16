#include "shelly_measurement.h"
#include "config.h"

#include "shelly_device.h"
#include "shelly_http.h"
#include "shelly_json.h"
#include "shelly_protocol.h"

#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#define TAG "SHELLY_MEASUREMENT"

/* Last measurement received from the Shelly. */
static shelly_measurement_t s_measurement;

/* Indicates whether the latest Shelly measurement is valid. */
static bool s_measurement_valid;

/* Protects concurrent access to the Shelly measurement state. */
static SemaphoreHandle_t s_measurement_mutex;

/* Generation-specific measurement update functions. */
/* Updates a Gen1 Shelly measurement into the provided local structure. */
static bool shelly_measurement_update_gen1(
    shelly_measurement_t *measurement)
{
    char response[2048];

    /* Reject invalid output pointers. */
    if (measurement == NULL)
    {
        return false;
    }

    /* Request the current electrical status from the Gen1 Shelly. */
    if (!shelly_http_get(
            SHELLY_URI_GEN1_STATUS,
            response,
            sizeof(response)))
    {
        return false;
    }

    /* Parse the response into the local measurement structure. */
    if (!shelly_json_parse_gen1_measurement(
            response,
            measurement))
    {
        return false;
    }

    return true;
}

/* Updates a Gen2 V1 Shelly measurement into the provided local structure. */
static bool shelly_measurement_update_gen2_v1(
    shelly_measurement_t *measurement)
{
    char response[2048];

    /* Reject invalid output pointers. */
    if (measurement == NULL)
    {
        return false;
    }

    /* Request the current electrical status from the Gen2 V1 Shelly. */
    if (!shelly_http_post(
            SHELLY_URI_GEN2_RPC,
            SHELLY_RPC_GET_STATUS,
            response,
            sizeof(response)))
    {
        return false;
    }

    /* Parse the response into the local measurement structure. */
    if (!shelly_json_parse_gen2_v1_measurement(
            response,
            measurement))
    {
        return false;
    }

    return true;
}

/* Updates a Gen2 V2 Shelly measurement into the provided local structure. */
static bool shelly_measurement_update_gen2_v2(
    shelly_measurement_t *measurement)
{
    char response[2048];

    /* Reject invalid output pointers. */
    if (measurement == NULL)
    {
        return false;
    }

    /* Request the current electrical status from the Gen2 V2 Shelly. */
    if (!shelly_http_post(
            SHELLY_URI_GEN2_RPC,
            SHELLY_RPC_EM_GET_STATUS,
            response,
            sizeof(response)))
    {
        return false;
    }

    /* Parse the response into the local measurement structure. */
    if (!shelly_json_parse_gen2_v2_measurement(
            response,
            measurement))
    {
        return false;
    }

    return true;
}

/* Initializes the Shelly measurement module. */
bool shelly_measurement_init(void)
{
    /* No valid measurement is available at startup. */
    s_measurement_valid =
        false;

    /* Clear the initial electrical measurement values. */
    memset(
        &s_measurement,
        0,
        sizeof(s_measurement));

    /* Create the mutex protecting the shared measurement state. */
    s_measurement_mutex =
        xSemaphoreCreateMutex();

    if (s_measurement_mutex == NULL)
    {
        logger_error(
            TAG,
            "Unable to create measurement mutex");

        return false;
    }

    logger_info(
        TAG,
        "Measurement module initialized");

    return true;
}

/* Updates the Shelly electrical measurements. */
bool shelly_measurement_update(void)
{
    /* Store the new measurement locally until the update is complete. */
    shelly_measurement_t measurement;
    const uint8_t phase_at_start = config_get()->shelly_phase;

    /* Read the detected device generation from a protected snapshot. */
    shelly_device_info_t device_info = {0};

    bool success =
        false;

    /* Clear the local measurement before parsing a new response. */
    memset(
        &measurement,
        0,
        sizeof(measurement));

    /* Leave the generation unknown if device information is unavailable. */
    if (!shelly_device_get_info(
            &device_info))
    {
        logger_warn(
            TAG,
            "Shelly device information unavailable");
    }

    /* Update the measurement according to the detected Shelly generation. */
    switch (device_info.generation)
    {
        case SHELLY_GENERATION_GEN1:

            success =
                shelly_measurement_update_gen1(
                    &measurement);

            break;

        case SHELLY_GENERATION_GEN2_V1:

            success =
                shelly_measurement_update_gen2_v1(
                    &measurement);

            break;

        case SHELLY_GENERATION_GEN2_V2:

            success =
                shelly_measurement_update_gen2_v2(
                    &measurement);

            break;

        default:

            logger_warn(
                TAG,
                "Unsupported Shelly generation");

            success =
                false;

            break;
    }

    /* Protect the shared state while publishing the update result. */
    xSemaphoreTake(
        s_measurement_mutex,
        portMAX_DELAY);

    if (phase_at_start != config_get()->shelly_phase) success = false;

    if (success)
    {
        /* Publish the complete measurement only after a successful update. */
        s_measurement =
            measurement;
    }

    /* Publish measurement validity together with the measurement state. */
    s_measurement_valid =
        success;

    xSemaphoreGive(
        s_measurement_mutex);

    if (success)
    {
        logger_debug(
            TAG,
            "U=%.1fV I=%.2fA P=%.1fW E=%.1fWh",
            measurement.voltage,
            measurement.current,
            measurement.power,
            measurement.energy);
    }

    return success;
}

/*
 * Returns a consistent snapshot of the latest Shelly measurement.
 *
 * The RS485 Modbus task may request a measurement before the Shelly
 * measurement module has been initialized. In that case, return an invalid
 * zeroed snapshot instead of attempting to use a null mutex.
 */
bool shelly_measurement_get(
    shelly_measurement_t *measurement)
{
    bool measurement_valid;

    /* Reject invalid output pointers. */
    if (measurement == NULL)
    {
        return false;
    }

    /*
     * No measurement can be returned before initialization has created the
     * mutex. Clear the caller's snapshot so no stale stack data can be used.
     */
    if (s_measurement_mutex == NULL)
    {
        memset(
            measurement,
            0,
            sizeof(*measurement));

        return false;
    }

    /* Protect the shared state while copying the complete measurement. */
    xSemaphoreTake(
        s_measurement_mutex,
        portMAX_DELAY);

    measurement_valid =
        s_measurement_valid;

    *measurement =
        s_measurement;

    xSemaphoreGive(
        s_measurement_mutex);

    return measurement_valid;
}

void shelly_measurement_invalidate(void)
{
    if (s_measurement_mutex == NULL)
    {
        return;
    }

    xSemaphoreTake(s_measurement_mutex, portMAX_DELAY);
    s_measurement_valid = false;
    xSemaphoreGive(s_measurement_mutex);
}
