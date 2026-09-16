#include "api_energy.h"

#include "api.h"

#include "power_manager.h"
#include "json_utils.h"

#define API_ENERGY_RESPONSE_SIZE 256

/* Handles GET /api/energy requests. */
static esp_err_t api_energy_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    char response[API_ENERGY_RESPONSE_SIZE];

    json_writer_t json;

    power_manager_status_t power_status;

    /* Retrieve a consistent snapshot of the power manager state. */
    if (!power_manager_get_status(
            &power_status))
    {
        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Unable to read power status");
    }

    json_init(
        &json,
        response,
        sizeof(response));

    json_begin_object(
        &json);

    /* Report whether the latest Shelly measurement is valid. */
    json_write_bool(
        &json,
        "measurement_valid",
        power_status.measurement_valid);

    /* Report the latest house current in amperes. */
    json_write_double(
        &json,
        "house_current",
        power_status.house_current_a);

    /* Report the latest house power in watts. */
    json_write_double(
        &json,
        "house_power",
        power_status.house_power_w);

    /* Convert the available charging current from cA to A. */
    json_write_double(
        &json,
        "available_current",
        power_status.available_current_ca / 100.0f);

    /* Convert the charging current limit from cA to A. */
    json_write_double(
        &json,
        "charge_limit",
        power_status.charge_limit_ca / 100.0f);

    /* Report whether charging is currently enabled. */
    json_write_bool(
        &json,
        "charging_enabled",
        power_status.charging_enabled);

    json_end_object(
        &json);

    if (!json_ok(
            &json))
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

static const httpd_uri_t energy_uri =
{
    .uri = "/api/energy",
    .method = HTTP_GET,
    .handler = api_energy_get_handler,
    .user_ctx = NULL
};

esp_err_t api_energy_register(
    httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &energy_uri);
}
