#include "shelly_http.h"

#include "shelly_config.h"

#include "logger.h"

#include "esp_http_client.h"

#include <stdio.h>
#include <string.h>

#define TAG "SHELLY_HTTP"

/* HTTP response buffer. */
typedef struct
{
    char *data;

    size_t length;

    size_t capacity;

    /* Indicates whether the HTTP response exceeded the buffer capacity. */
    bool truncated;

} shelly_http_buffer_t;

/* HTTP response context. */
static shelly_http_buffer_t s_response;

static bool shelly_http_build_url(
    const char *uri,
    char *url,
    size_t size);

/* Handles HTTP client events. */
static esp_err_t shelly_http_event_handler(
    esp_http_client_event_t *event);

/* Performs an HTTP request. */
static bool shelly_http_request(
    esp_http_client_method_t method,
    const char *uri,
    const char *body,
    char *response,
    size_t response_size);

/* Builds the complete request URL. */
static bool shelly_http_build_url(
    const char *uri,
    char *url,
    size_t size)
{
    if ((uri == NULL) ||
        (url == NULL) ||
        (size == 0))
    {
        return false;
    }

    int written =
        snprintf(
            url,
            size,
            "http://%s:%u%s",
            shelly_config_get_host(),
            shelly_config_get_port(),
            uri);

    if ((written < 0) ||
        ((size_t)written >= size))
    {
        return false;
    }

    return true;
}

/* Handles HTTP client events. */
static esp_err_t shelly_http_event_handler(
    esp_http_client_event_t *event)
{
    if (event == NULL)
    {
        return ESP_FAIL;
    }

    switch (event->event_id)
    {
        case HTTP_EVENT_ON_DATA:

            if ((event->data == NULL) ||
                (event->data_len == 0))
            {
                break;
            }

            if ((s_response.data == NULL) ||
                (s_response.capacity == 0))
            {
                break;
            }

            /* Reject additional data when the response buffer is already full. */
            if ((s_response.capacity <= 1) ||
                (s_response.length >=
                    (s_response.capacity - 1)))
            {
                /* Additional response data means the payload is truncated. */
                if (event->data_len > 0)
                {
                    s_response.truncated =
                        true;
                }

                break;
            }

            size_t remaining =
                (s_response.capacity - 1) -
                s_response.length;

            size_t copy =
                event->data_len;

            /* Detect when the HTTP response exceeds the available buffer. */
            if (copy > remaining)
            {
                s_response.truncated =
                    true;

                copy =
                    remaining;
            }

            memcpy(
                &s_response.data[
                    s_response.length],
                event->data,
                copy);

            s_response.length +=
                copy;

            s_response.data[
                s_response.length] =
                    '\0';

            break;

        default:

            break;
    }

    return ESP_OK;
}

/* Performs an HTTP request. */
static bool shelly_http_request(
    esp_http_client_method_t method,
    const char *uri,
    const char *body,
    char *response,
    size_t response_size)
{
    char url[256];

    if ((uri == NULL) ||
        (response == NULL) ||
        (response_size == 0))
    {
        return false;
    }

    /* Builds the complete request URL. */
    if (!shelly_http_build_url(
            uri,
            url,
            sizeof(url)))
    {
        logger_warn(
            TAG,
            "Unable to build request URL");

        return false;
    }
    
    logger_debug(
        TAG,
        "Request URL: %s",
        url);

    esp_http_client_config_t config =
    {
        .url = url,
        .event_handler =
            shelly_http_event_handler,
        .timeout_ms =
            shelly_config_get_timeout()
    };

    /* Initializes the HTTP response buffer. */
    s_response.data =
        response;

    s_response.length =
        0;

    s_response.capacity =
        response_size;

    /* No response truncation has occurred yet. */
    s_response.truncated =
        false;

    /* Start with an empty response string. */
    response[0] =
        '\0';

    esp_http_client_handle_t client =
        esp_http_client_init(
            &config);

    if (client == NULL)
    {
        logger_warn(
            TAG,
            "Unable to create HTTP client");

        return false;
    }

    esp_err_t err =
        esp_http_client_set_method(
            client,
            method);

    if (err != ESP_OK)
    {
        logger_warn(
            TAG,
            "Unable to configure HTTP method (%s)",
            esp_err_to_name(err));

        esp_http_client_cleanup(
            client);

        return false;
    }

    /* Configure HTTP Basic authentication when credentials are available. */
    const char *username =
        shelly_config_get_username();

    const char *password =
        shelly_config_get_password();

    if ((username != NULL) &&
        (password != NULL) &&
        (username[0] != '\0') &&
        (password[0] != '\0'))
    {
        err =
            esp_http_client_set_username(
                client,
                username);

        if (err != ESP_OK)
        {
            logger_warn(
                TAG,
                "Unable to configure HTTP username (%s)",
                esp_err_to_name(err));

            esp_http_client_cleanup(
                client);

            return false;
        }

        err =
            esp_http_client_set_password(
                client,
                password);

        if (err != ESP_OK)
        {
            logger_warn(
                TAG,
                "Unable to configure HTTP password (%s)",
                esp_err_to_name(err));

            esp_http_client_cleanup(
                client);

            return false;
        }

        err =
            esp_http_client_set_authtype(
                client,
                HTTP_AUTH_TYPE_BASIC);

        if (err != ESP_OK)
        {
            logger_warn(
                TAG,
                "Unable to configure HTTP authentication (%s)",
                esp_err_to_name(err));

            esp_http_client_cleanup(
                client);

            return false;
        }

        logger_debug(
            TAG,
            "HTTP Basic authentication enabled");
    }
    else
    {
        logger_debug(
            TAG,
            "HTTP authentication disabled");
    }

    /* Configure POST payload. */
    if ((method == HTTP_METHOD_POST) &&
        (body != NULL))
    {
        err =
            esp_http_client_set_header(
                client,
                "Content-Type",
                "application/json");

        if (err != ESP_OK)
        {
            logger_warn(
                TAG,
                "Unable to configure HTTP Content-Type (%s)",
                esp_err_to_name(err));

            esp_http_client_cleanup(
                client);

            return false;
        }

        err =
            esp_http_client_set_post_field(
                client,
                body,
                strlen(body));

        if (err != ESP_OK)
        {
            logger_warn(
                TAG,
                "Unable to configure HTTP POST payload (%s)",
                esp_err_to_name(err));

            esp_http_client_cleanup(
                client);

            return false;
        }

        logger_debug(
            TAG,
            "POST payload: %s",
            body);
    }

    logger_debug(
        TAG,
        "Sending HTTP %s",
        (method == HTTP_METHOD_GET) ?
            "GET" :
            "POST"); 

    err =
        esp_http_client_perform(
            client);

    if (err != ESP_OK)
    {
        logger_warn(
            TAG,
            "HTTP request failed (%s)",
            esp_err_to_name(err));

        esp_http_client_cleanup(
            client);

        return false;
    }

    int status =
        esp_http_client_get_status_code(
            client);

    logger_debug(
        TAG,
        "HTTP status %d",
        status);

    if ((status < 200) ||
        (status >= 300))
    {
        logger_warn(
            TAG,
            "Unexpected HTTP status %d",
            status);

        esp_http_client_cleanup(
            client);

        return false;
    }

    logger_debug(
        TAG,
        "HTTP response: %u bytes",
        (unsigned)s_response.length);

    /* Reject an incomplete HTTP response. */
    if (s_response.truncated)
    {
        logger_warn(
            TAG,
            "HTTP response truncated");

        esp_http_client_cleanup(
            client);

        return false;
    }

    /* Reject an empty HTTP response. */
    if (s_response.length == 0)
    {
        logger_warn(
            TAG,
            "HTTP response is empty");

        esp_http_client_cleanup(
            client);

        return false;
    }

    esp_http_client_cleanup(
        client);

    return true;
}

/* Performs an HTTP GET request. */
bool shelly_http_get(
    const char *uri,
    char *response,
    size_t response_size)
{
    return shelly_http_request(
        HTTP_METHOD_GET,
        uri,
        NULL,
        response,
        response_size);
}

/* Performs an HTTP POST request. */
bool shelly_http_post(
    const char *uri,
    const char *body,
    char *response,
    size_t response_size)
{
    return shelly_http_request(
        HTTP_METHOD_POST,
        uri,
        body,
        response,
        response_size);
}