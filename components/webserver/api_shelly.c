#include "api_shelly.h"

#include "api.h"
#include "config.h"
#include "json_utils.h"

#include "shelly_device.h"
#include "shelly_measurement.h"

#include <string.h>

#define API_SHELLY_RESPONSE_SIZE 1024

/* Handles GET requests for the current Shelly status. */
static esp_err_t api_shelly_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    char response[API_SHELLY_RESPONSE_SIZE];

    shelly_measurement_t measurement;

        /* Read all detected device fields from the same snapshot. */
    shelly_device_info_t device_info;

    if (!shelly_device_get_info(
            &device_info))
    {
        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Shelly device information unavailable");
    }

    /* Read one consistent snapshot of the latest Shelly measurement. */
    bool measurement_valid =
        shelly_measurement_get(
            &measurement);

    json_writer_t json;

    json_init(
        &json,
        response,
        sizeof(response));

    json_begin_object(&json);

    /* Report whether the latest Shelly measurement is valid. */
    json_write_bool(
        &json,
        "measurement_valid",
        measurement_valid);

    json_write_uint(&json, "shelly_phase", config_get()->shelly_phase);

    /* Report fields from the same detected device snapshot. */
    json_write_int(
        &json,
        "generation",
        device_info.generation);

    json_write_string(
        &json,
        "model",
        device_info.model);

    json_write_string(
        &json,
        "firmware",
        device_info.firmware);

    /* Export values from the same Shelly measurement snapshot. */
    json_write_double(
        &json,
        "power",
        measurement_valid ?
            measurement.power :
            0.0);

    json_write_double(
        &json,
        "current",
        measurement_valid ?
            measurement.current :
            0.0);

    json_end_object(&json);

    /* Reject responses that exceed the fixed JSON output buffer. */
    if (!json_ok(&json))
    {
        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "JSON overflow");
    }

    return api_send_json(
        req,
        response);
}

static const httpd_uri_t shelly_uri =
{
    .uri = "/api/shelly",
    .method = HTTP_GET,
    .handler = api_shelly_get_handler,
    .user_ctx = NULL
};

esp_err_t api_shelly_register(
    httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &shelly_uri);
}
