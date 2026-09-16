#include "api_watchdog.h"

#include "api.h"
#include "json_utils.h"
#include "watchdog.h"

#define API_WATCHDOG_RESPONSE_SIZE 1024

/* Handles GET /api/health/watchdog requests. */
static esp_err_t api_watchdog_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    char response[API_WATCHDOG_RESPONSE_SIZE];

    json_writer_t json;

    uint32_t registered =
        0;

    uint32_t alive =
        0;

    /* Count the currently registered and alive watchdog tasks. */
    for (int task = 0; task < WD_MAX_TASKS; task++)
    {
        watchdog_status_t status;

        if (!watchdog_get_status(
                (watchdog_task_t)task,
                &status))
        {
            continue;
        }

        if (!status.registered)
        {
            continue;
        }

        registered++;

        if (status.alive)
        {
            alive++;
        }
    }

    json_init(
        &json,
        response,
        API_WATCHDOG_RESPONSE_SIZE);

    json_begin_object(
        &json);

    json_write_uint(
        &json,
        "registered_tasks",
        registered);

    json_write_uint(
        &json,
        "alive_tasks",
        alive);

    json_begin_named_array(
        &json,
        "tasks");

    for (int task = 0; task < WD_MAX_TASKS; task++)
    {
        watchdog_status_t status;

        if (!watchdog_get_status(
                (watchdog_task_t)task,
                &status))
        {
            continue;
        }

        if (!status.registered)
        {
            continue;
        }

        json_array_begin_object(
            &json);

        json_write_string(
            &json,
            "name",
            watchdog_task_name(
                (watchdog_task_t)task));

        json_write_bool(
            &json,
            "registered",
            status.registered);

        json_write_bool(
            &json,
            "alive",
            status.alive);

        json_write_uint(
            &json,
            "timeout_count",
            status.timeout_count);

        json_write_uint(
            &json,
            "recovery_count",
            status.recovery_count);

        json_write_uint64(
            &json,
            "last_feed_ms",
            status.last_feed_ms);

        json_array_end_object(
            &json);
    }

    json_end_array(
        &json);

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

static const httpd_uri_t watchdog_uri =
{
    .uri = "/api/health/watchdog",
    .method = HTTP_GET,
    .handler = api_watchdog_get_handler,
    .user_ctx = NULL
};

esp_err_t api_watchdog_register(
    httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &watchdog_uri);
}
