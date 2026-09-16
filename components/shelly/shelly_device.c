#include "shelly_device.h"

#include "shelly_config.h"
#include "shelly_http.h"
#include "shelly_protocol.h"

#include "logger.h"

#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#define TAG "SHELLY_DEVICE"

/* Current Shelly generation. */
static shelly_generation_t s_generation =
    SHELLY_GENERATION_UNKNOWN;

/* Shelly model. */
static char s_model[32];

/* Shelly firmware version. */
static char s_firmware[32];

/* Protects concurrent access to the detected Shelly device information. */
static SemaphoreHandle_t s_device_mutex;

/* Detects a Shelly Gen1 device into a local device snapshot. */
static bool shelly_device_detect_gen1(
    char *response,
    size_t response_size,
    shelly_device_info_t *info);

/* Detects a Shelly Gen2 device into a local device snapshot. */
static bool shelly_device_detect_gen2(
    char *response,
    size_t response_size,
    shelly_device_info_t *info);

/* Detects the supported Gen2 measurement interface. */
static bool shelly_device_detect_gen2_generation(
    char *response,
    size_t response_size,
    shelly_device_info_t *info);

/* Parses a Shelly Gen1 device description into a local snapshot. */
static bool shelly_device_parse_gen1(
    const char *response,
    shelly_device_info_t *info);

/* Parses a Shelly Gen2 device description into a local snapshot. */
static bool shelly_device_parse_gen2(
    const char *response,
    shelly_device_info_t *info);

/* Detects the connected Shelly device. */
bool shelly_device_detect(void)
{
    /* Response buffer large enough for Gen2 status payloads. */
    char response[2048];

    /* Build the detection result without exposing partial information. */
    shelly_device_info_t info = {0};

    if (s_device_mutex == NULL)
    {
        logger_error(
            TAG,
            "Shelly device module is not initialized");

        return false;
    }

    /* Wait for an explicit web configuration instead of probing a stale IP. */
    const char *host =
        shelly_config_get_host();

    if ((host == NULL) ||
        (host[0] == '\0'))
    {
        return false;
    }

    /* Clear previously detected information before starting detection. */
    xSemaphoreTake(
        s_device_mutex,
        portMAX_DELAY);

    s_generation =
        SHELLY_GENERATION_UNKNOWN;

    s_model[0] =
        '\0';

    s_firmware[0] =
        '\0';

    xSemaphoreGive(
        s_device_mutex);

    memset(
        response,
        0,
        sizeof(response));

    /* Try Shelly Gen2 first. */
    if (!shelly_device_detect_gen2(
            response,
            sizeof(response),
            &info))
    {
        /* Discard any partial Gen2 result before trying Gen1. */
        memset(
            &info,
            0,
            sizeof(info));

        memset(
            response,
            0,
            sizeof(response));

        /* Fallback to Shelly Gen1. */
        if (!shelly_device_detect_gen1(
                response,
                sizeof(response),
                &info))
        {
            logger_warn(
                TAG,
                "Unable to detect Shelly device");

            return false;
        }
    }

    /* Publish the complete detection result in one critical section. */
    xSemaphoreTake(
        s_device_mutex,
        portMAX_DELAY);

    s_generation =
        info.generation;

    strlcpy(
        s_model,
        info.model,
        sizeof(s_model));

    strlcpy(
        s_firmware,
        info.firmware,
        sizeof(s_firmware));

    xSemaphoreGive(
        s_device_mutex);

    return true;
}

/* Detects a Shelly Gen1 device into a local device snapshot. */
static bool shelly_device_detect_gen1(
    char *response,
    size_t response_size,
    shelly_device_info_t *info)
{
    /* Request the Gen1 device information endpoint. */
    if (!shelly_http_get(
            SHELLY_URI_GEN1_INFO,
            response,
            response_size))
    {
        return false;
    }

    if (!shelly_device_parse_gen1(
            response,
            info))
    {
        logger_warn(
            TAG,
            "Unable to parse Gen1 information");

        return false;
    }

    logger_info(
        TAG,
        "Shelly Gen1 detected (%s)",
        info->model);

    return true;
}

/* Detects a Shelly Gen2 device into a local device snapshot. */
static bool shelly_device_detect_gen2(
    char *response,
    size_t response_size,
    shelly_device_info_t *info)
{
    /* Request the Gen2 device information through the RPC endpoint. */
    if (!shelly_http_post(
            SHELLY_URI_GEN2_RPC,
            SHELLY_RPC_DEVICE_INFO,
            response,
            response_size))
    {
        return false;
    }

    if (!shelly_device_parse_gen2(
            response,
            info))
    {
        logger_warn(
            TAG,
            "Unable to parse Gen2 information");

        /* Show the response shape to diagnose the parser failure. */
        logger_warn(
            TAG,
            "Gen2 device info response: %.512s",
            response);
            
        return false;
    }

    /* Detect the Gen2 measurement interface used by the device. */
    if (!shelly_device_detect_gen2_generation(
            response,
            response_size,
            info))
    {
        logger_warn(
            TAG,
            "Unable to detect Gen2 measurement interface");

        return false;
    }

    logger_info(
        TAG,
        "Shelly Gen2 detected (%s)",
        info->model);

    return true;
}

/* Detects the supported Gen2 measurement interface. */
static bool shelly_device_detect_gen2_generation(
    char *response,
    size_t response_size,
    shelly_device_info_t *info)
{
    /* Request the complete Gen2 component status. */
    if (!shelly_http_post(
            SHELLY_URI_GEN2_RPC,
            SHELLY_RPC_GET_STATUS,
            response,
            response_size))
    {
        return false;
    }

    cJSON *root =
        cJSON_Parse(response);

    if (root == NULL)
    {
        return false;
    }

    /* RPC responses contain component status inside "result". */
    cJSON *result =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "result");

    if (!cJSON_IsObject(result))
    {
        cJSON_Delete(
            root);

        return false;
    }

    cJSON *em =
        cJSON_GetObjectItemCaseSensitive(
            result,
            "em:0");

    cJSON *sw =
        cJSON_GetObjectItemCaseSensitive(
            result,
            "switch:0");

    if (cJSON_IsObject(em))
    {
        info->generation =
            SHELLY_GENERATION_GEN2_V2;
    }
    else if (cJSON_IsObject(sw))
    {
        info->generation =
            SHELLY_GENERATION_GEN2_V1;
    }
    else
    {
        cJSON_Delete(
            root);

        return false;
    }

    cJSON_Delete(
        root);

    return true;
}

/* Initializes the Shelly device module. */
bool shelly_device_init(void)
{
    /* Reset the detected device information before initialization. */
    s_generation =
        SHELLY_GENERATION_UNKNOWN;

    s_model[0] =
        '\0';

    s_firmware[0] =
        '\0';

    /* Create the mutex protecting the detected device information. */
    s_device_mutex =
        xSemaphoreCreateMutex();

    if (s_device_mutex == NULL)
    {
        logger_error(
            TAG,
            "Unable to create device mutex");

        return false;
    }

    logger_info(
        TAG,
        "Shelly device initialized");

    return true;
}

/* Returns a consistent snapshot of the detected Shelly device information. */
bool shelly_device_get_info(
    shelly_device_info_t *info)
{
    /* Reject invalid output pointers or uninitialized access. */
    if ((info == NULL) ||
        (s_device_mutex == NULL))
    {
        return false;
    }

    /* Protect all fields so they belong to the same device snapshot. */
    xSemaphoreTake(
        s_device_mutex,
        portMAX_DELAY);

    info->generation =
        s_generation;

    strlcpy(
        info->model,
        s_model,
        sizeof(info->model));

    strlcpy(
        info->firmware,
        s_firmware,
        sizeof(info->firmware));

    xSemaphoreGive(
        s_device_mutex);

    return true;
}

/* Parses a Shelly Gen1 device description into a local snapshot. */
static bool shelly_device_parse_gen1(
    const char *response,
    shelly_device_info_t *info)
{
    cJSON *root =
        cJSON_Parse(response);

    if (root == NULL)
    {
        return false;
    }

    cJSON *type =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "type");

    cJSON *fw =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "fw");

    /* A valid non-empty Gen1 device type is required for detection. */
    if (!cJSON_IsString(type) ||
        (type->valuestring == NULL) ||
        (type->valuestring[0] == '\0'))
    {
        cJSON_Delete(
            root);

        return false;
    }

    /* Store the validated model in the local snapshot. */
    strncpy(
        info->model,
        type->valuestring,
        sizeof(info->model) - 1);

    info->model[
        sizeof(info->model) - 1] = '\0';

    if (cJSON_IsString(fw))
    {
        strncpy(
            info->firmware,
            fw->valuestring,
            sizeof(info->firmware) - 1);

        info->firmware[
            sizeof(info->firmware) - 1] = '\0';
    }

    info->generation =
        SHELLY_GENERATION_GEN1;

    cJSON_Delete(
        root);

    return true;
}

/* Parses a Shelly Gen2 device description into a local snapshot. */
static bool shelly_device_parse_gen2(
    const char *response,
    shelly_device_info_t *info)
{
    cJSON *root =
        cJSON_Parse(response);

    if (root == NULL)
    {
        return false;
    }

    /* The RPC response stores device fields inside "result". */
    cJSON *result =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "result");

    if (!cJSON_IsObject(result))
    {
        cJSON_Delete(
            root);

        return false;
    }
    
    cJSON *model =
        cJSON_GetObjectItemCaseSensitive(
            result,
            "model");

    cJSON *fw =
        cJSON_GetObjectItemCaseSensitive(
            result,
            "fw_id");

    /* A valid non-empty Gen2 device model is required for detection. */
    if (!cJSON_IsString(model) ||
        (model->valuestring == NULL) ||
        (model->valuestring[0] == '\0'))
    {
        cJSON_Delete(
            root);

        return false;
    }

    /* Store the validated model in the local snapshot. */
    strncpy(
        info->model,
        model->valuestring,
        sizeof(info->model) - 1);

    info->model[
        sizeof(info->model) - 1] = '\0';

    if (cJSON_IsString(fw))
    {
        strncpy(
            info->firmware,
            fw->valuestring,
            sizeof(info->firmware) - 1);

        info->firmware[
            sizeof(info->firmware) - 1] = '\0';
    }

    cJSON_Delete(
        root);

    return true;
}
