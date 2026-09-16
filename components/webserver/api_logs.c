#include "api_logs.h"
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "api.h"
#include "esp_http_server.h"
#include "json_utils.h"

#define API_LOG_LINE_SIZE 768

/* Formats a log entry as a JSON object. */
static int api_logs_format_entry(
    char *buffer,
    size_t size,
    const logger_entry_t *entry,
    bool first);

/* Sends one HTTP response chunk. */
static esp_err_t api_logs_send_chunk(
    httpd_req_t *req,
    const char *data,
    ssize_t len);

/* Sends one HTTP response chunk. */
static esp_err_t api_logs_send_chunk(
    httpd_req_t *req,
    const char *data,
    ssize_t len)
{
    return httpd_resp_send_chunk(
        req,
        data,
        len);
}

/* Formats one log entry as JSON. */
static int api_logs_format_entry(
    char *buffer,
    size_t size,
    const logger_entry_t *entry,
    bool first)
{
    char module[LOGGER_MAX_MODULE_LENGTH * 2];
    char message[LOGGER_MAX_MESSAGE_LENGTH * 2];

    json_escape_string(
        module,
        sizeof(module),
        entry->module);

    json_escape_string(
        message,
        sizeof(message),
        entry->message);

    return snprintf(
        buffer,
        size,
        "%s"
        "{"
        "\"timestamp\":%llu,"
        "\"level\":%u,"
        "\"module\":\"%s\","
        "\"message\":\"%s\""
        "}",
        first ? "" : ",",
        (unsigned long long)entry->timestamp_us,
        (unsigned)entry->level,
        module,
        message);
}

/* Endpoint exposing the in-memory log buffer as a JSON array. */
static esp_err_t api_logs_get_handler(httpd_req_t *req);

/* Log endpoint descriptor. */
static const httpd_uri_t logs_uri =
{
    .uri      = "/api/logs",
    .method   = HTTP_GET,
    .handler  = api_logs_get_handler,
    .user_ctx = NULL
};

/* Registers the log endpoint. */
esp_err_t api_logs_register(httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &logs_uri);
}

/* Handles GET /api/logs requests. */
static esp_err_t api_logs_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    logger_entry_t entry;
    char line[API_LOG_LINE_SIZE];     

    httpd_resp_set_type(
        req,
        "application/json");

    esp_err_t err;

    err = api_logs_send_chunk(
        req,
        "[",
        1);

    if (err != ESP_OK)
    {
        return err;
    }

    uint16_t count = logger_count();

    for (uint16_t i = 0; i < count; i++)
    {
        memset(
            &entry,
            0,
            sizeof(entry));

        if (!logger_get(i, &entry))
        {
            continue;
        }

        int len = api_logs_format_entry(
            line,
            sizeof(line),
            &entry,
            (i == 0));

        if (len < 0)
        {
            return ESP_FAIL;
        }

        if ((size_t)len >= sizeof(line))
        {
            return httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "log line too long");
        }

        err = api_logs_send_chunk(
            req,
            line,
            len);

        if (err != ESP_OK)
        {
            logger_warn(
                "API",
                "HTTP chunk send failed (%s)",
                esp_err_to_name(err));

            return err;
        }
    }

    err = api_logs_send_chunk(
        req,
        "]",
        1);

    if (err != ESP_OK)
    {
        return err;
    }

    return api_logs_send_chunk(
        req,
        NULL,
        0);
}
