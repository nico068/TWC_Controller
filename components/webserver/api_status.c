#include "api_status.h"
#include "api.h"
#include "rs485.h"
#include <stdio.h>

#include "json_utils.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "shelly_device.h"

#include "esp_http_server.h"
#include "esp_netif.h"

#define API_STATUS_RESPONSE_SIZE    512

/* Writes the current system status as JSON. */
static void api_status_write_json(
    json_writer_t *json,
    const wifi_manager_status_t *wifi)
{
    power_manager_status_t power_status;

    /* Reject invalid input pointers. */
    if ((json == NULL) || (wifi == NULL))
    {
        return;
    }

    /* Read one consistent snapshot of the power manager status. */
    if (!power_manager_get_status(
            &power_status))
    {
        return;
    }

    char ip[20];

    if (wifi->got_ip)
    {
        snprintf(
            ip,
            sizeof(ip),
            IPSTR,
            IP2STR(&wifi->ip.ip));
    }
    else
    {
        strlcpy(
            ip,
            "0.0.0.0",
            sizeof(ip));
    }

    json_write_bool(
        json,
        "wifi_connected",
        wifi->got_ip);

    json_write_int(
        json,
        "wifi_state",
        (int)wifi->state);

    json_write_string(
        json,
        "ip",
        ip);

    /* Report a connected Wall Connector only while valid Gen 3 Modbus requests are being received on RS485. */
    json_write_bool(
        json,
        "wall_connector_connected",
        rs485_wall_connector_is_connected());

    /* Export the power manager snapshot. */
    json_write_bool(
        json,
        "measurement_valid",
        power_status.measurement_valid);

    json_write_bool(
        json,
        "charging_enabled",
        power_status.charging_enabled);

    json_write_double(
        json,
        "charge_limit",
        power_status.charge_limit_ca /
        100.0f);

    /* Read both Shelly fields from the same protected snapshot. */
    shelly_device_info_t device_info;

    if (!shelly_device_get_info(
            &device_info))
    {
        return;
    }

    json_write_string(
        json,
        "shelly_model",
        device_info.model);

    json_write_string(
        json,
        "shelly_firmware",
        device_info.firmware);
}


static esp_err_t api_status_get_handler(
    httpd_req_t *req);

/* Status endpoint descriptor. */
static const httpd_uri_t status_uri =
{
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_get_handler,
    .user_ctx = NULL
};

/* Registers the status endpoint. */
esp_err_t api_status_register(
    httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &status_uri);
}

/* Endpoint returning the current WiFi status and IP address. */
static esp_err_t api_status_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    const wifi_manager_status_t *wifi =
        wifi_manager_get_status();

    char response[API_STATUS_RESPONSE_SIZE];    

    json_writer_t json;

    json_init(
        &json,
        response,
        API_STATUS_RESPONSE_SIZE);

    json_begin_object(&json);

    api_status_write_json(
        &json,
        wifi);

    json_end_object(&json);

    if (!json_ok(&json))
    {
        return httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "JSON overflow");
    }

    static const char JSON_CONTENT_TYPE[] =
        "application/json";
        
    httpd_resp_set_type(
    req,
    JSON_CONTENT_TYPE);

    return httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN); 
}
