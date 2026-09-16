#include "api_health.h"

#include "api.h"
#include "config.h"
#include "power_manager.h"
#include "json_utils.h"
#include "rs485.h"

#include "watchdog.h"
#include "webserver.h"
#include "wifi_manager.h"

#include <stdlib.h>
#include <string.h>

#define API_HEALTH_RESPONSE_SIZE 4096

static bool api_health_watchdog_task_alive(
    watchdog_task_t task)
{
    watchdog_status_t status;

    if (!watchdog_get_status(task, &status))
    {
        return false;
    }

    if (!status.registered)
    {
        return true;
    }

    return status.alive;
}

static const char *api_health_overall_status(
    bool wifi_connected,
    bool webserver_running,
    bool shelly_connected,
    bool shelly_required)
{
    if (!wifi_connected)
    {
        return "degraded";
    }

    if (!webserver_running)
    {
        return "degraded";
    }

    if (!api_health_watchdog_task_alive(WD_SUPERVISOR))
    {
        return "degraded";
    }

    if (!api_health_watchdog_task_alive(WD_SHELLY))
    {
        return "degraded";
    }

    if (shelly_required && !shelly_connected)
    {
        return "degraded";
    }

    return "ok";
}

static void api_health_write_degraded_reasons(
    json_writer_t *json,
    bool wifi_connected,
    bool webserver_running,    
    bool shelly_connected,
    bool shelly_required)
{
    json_begin_named_array(json, "degraded_reasons");

    if (!wifi_connected)
    {
        json_array_string(json, "wifi_disconnected");
    }

    if (!webserver_running)
    {
        json_array_string(json, "webserver_not_running");
    }

    if (!api_health_watchdog_task_alive(WD_SUPERVISOR))
    {
        json_array_string(json, "watchdog_supervisor_timeout");
    }

    if (!api_health_watchdog_task_alive(WD_SHELLY))
    {
        json_array_string(json, "watchdog_shelly_timeout");
    }  

    if (shelly_required && !shelly_connected)
    {
        json_array_string(json, "shelly_disconnected");
    }

    json_end_array(json);
}

static void api_health_write_watchdog(
    json_writer_t *json)
{
    json_begin_named_array(
        json,
        "watchdog");

    for (int task = 0; task < WD_MAX_TASKS; task++)
    {
        watchdog_status_t status;

        if (!watchdog_get_status((watchdog_task_t)task, &status))
        {
            continue;
        }

        if (!status.registered)
        {
            continue;
        }

        json_array_begin_object(json);

        json_write_int(json, "id", task);
        json_write_string(
            json,
            "name",
            watchdog_task_name((watchdog_task_t)task));
        json_write_bool(json, "alive", status.alive);
        json_write_uint(json, "timeout_count", status.timeout_count);
        json_write_uint(json, "recovery_count", status.recovery_count);
        json_write_uint64(json, "last_feed_ms", status.last_feed_ms);

        json_array_end_object(json);
    }

    json_end_array(json);
}

/* Handles GET /api/health requests. */
static esp_err_t api_health_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    const wifi_manager_status_t *wifi =
        wifi_manager_get_status();

    wifi_manager_status_t wifi_fallback = {0};

    if (wifi == NULL)
    {
        wifi = &wifi_fallback;
    }

    power_manager_status_t power_status;

    if (!power_manager_get_status(&power_status))
    {
        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Power manager unavailable");
    }

    bool webserver_running =
        webserver_is_running();
    
    const system_config_t *cfg =
        config_get();

    bool shelly_required =
        cfg->shelly_host[0] != '\0';

    bool shelly_connected =
        power_status.measurement_valid;

    bool shelly_available =
        power_status.measurement_valid;

    bool shelly_parse_ready =
        power_status.measurement_valid;        

    char *response = calloc(1, API_HEALTH_RESPONSE_SIZE);

    if (response == NULL)
    {
        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "no memory");
    }

    json_writer_t json;

    json_init(
        &json,
        response,
        API_HEALTH_RESPONSE_SIZE);

    json_begin_object(&json);

    json_write_string(
        &json,
        "overall_status",
        api_health_overall_status(
            wifi->got_ip,
            webserver_running,            
            shelly_connected,
            shelly_required));

    api_health_write_degraded_reasons(
        &json,
        wifi->got_ip,
        webserver_running,        
        shelly_connected,
        shelly_required);

    json_write_bool(&json, "wifi_connected", wifi->got_ip);
    json_write_int(&json, "wifi_state", (int)wifi->state);
    json_write_bool(&json, "webserver_running", webserver_running);
    json_write_bool(
        &json,
        "wall_connector_connected",
        rs485_wall_connector_is_connected());
    
    json_write_bool(&json, "shelly_required", shelly_required);
    json_write_bool(&json, "shelly_connected", shelly_connected);
    json_write_bool(&json, "shelly_available", shelly_available);
    json_write_bool(&json, "shelly_parse_ready", shelly_parse_ready);
    
    json_begin_named_object(&json, "energy");
    json_write_bool(&json, "available", power_status.measurement_valid);
    json_write_double(&json, "house_current", power_status.house_current_a);
    json_write_double(&json, "house_power", power_status.house_power_w);
    json_write_double(&json, "available_current", power_status.available_current_ca / 100.0f);
    json_write_double(&json, "charge_limit", power_status.charge_limit_ca / 100.0f);
    /* Report whether charging is currently enabled. */
    json_write_bool(&json, "charging_enabled", power_status.charging_enabled);
    json_end_object(&json);

    api_health_write_watchdog(&json);

    json_end_object(&json);

    if (!json_ok(&json))
    {
        free(response);

        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "JSON overflow");
    }

    esp_err_t err = api_send_json(
        req,
        response);

    free(response);

    return err;
}

static const httpd_uri_t health_uri =
{
    .uri = "/api/health",
    .method = HTTP_GET,
    .handler = api_health_get_handler,
    .user_ctx = NULL
};

esp_err_t api_health_register(
    httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &health_uri);
}
