/*
 * Embedded HTTP server for the controller dashboard and API endpoints.
 * It hosts the index page and all REST routes registered from the components.
 */

#include "webserver.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_spiffs.h"

#include "logger.h"
#include "wifi_manager.h"
#include "routes.h"
#include "api.h"

static const char *TAG = "WEB";

#define WEBSERVER_PORT 80
#define WEBSERVER_MAX_URI_HANDLERS 20

/* Runtime state of the embedded HTTP server. */
typedef struct
{
    httpd_handle_t server;
    bool running;
} webserver_context_t;

static webserver_context_t s_webserver;

/* Handlers and helpers for static file serving from SPIFFS */
static esp_err_t static_file_handler(
    httpd_req_t *req)
{
    char filepath[600];

    /* Reject path traversal attempts in static file requests. */
    if (strstr(req->uri, "..") != NULL)
    {
        logger_warn(
            TAG,
            "Rejected invalid static file path: %s",
            req->uri);

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "400: Invalid path");

        return ESP_FAIL;
    }

    int path_length;

    /* Build the SPIFFS path and detect a truncated result. */
    if (strcmp(req->uri, "/") == 0)
    {
        path_length =
            snprintf(
                filepath,
                sizeof(filepath),
                "/spiffs/index.html");
    }
    else
    {
        path_length =
            snprintf(
                filepath,
                sizeof(filepath),
                "/spiffs%s",
                req->uri);
    }

    /* Reject paths that cannot fit completely in the local buffer. */
    if ((path_length < 0) ||
        ((size_t)path_length >= sizeof(filepath)))
    {
        logger_warn(
            TAG,
            "Static file path is too long");

        httpd_resp_send_err(
            req,
            HTTPD_414_URI_TOO_LONG,
            "414: URI Too Long");

        return ESP_FAIL;
    }

    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        logger_warn(
            TAG,
            "File not found: %s",
            filepath);

        httpd_resp_send_err(
            req,
            HTTPD_404_NOT_FOUND,
            "404: File Not Found");

        return ESP_FAIL;
    }

    // MIME type resolution
    if (strstr(filepath, ".html"))
    {
        httpd_resp_set_type(req, "text/html");
    }
    else if (strstr(filepath, ".css"))
    {
        httpd_resp_set_type(req, "text/css");
    }
    else if (strstr(filepath, ".js"))
    {
        httpd_resp_set_type(req, "application/javascript");
    }
    else if (strstr(filepath, ".json"))
    {
        httpd_resp_set_type(req, "application/json");
    }
    else if (strstr(filepath, ".ico"))
    {
        httpd_resp_set_type(req, "image/x-icon");
    }
    else if (strstr(filepath, ".svg"))
    {
        httpd_resp_set_type(req, "image/svg+xml");
    }
    else if (strstr(filepath, ".woff"))
    {
        httpd_resp_set_type(req, "font/woff");
    }
    else if (strstr(filepath, ".woff2"))
    {
        httpd_resp_set_type(req, "font/woff2");
    }

    /* HTML and scripts must refresh after a firmware/SPIFFS update. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // Stream content in chunks
    char chunk[1024];
    size_t read_bytes;

    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK)
        {
            fclose(file);
            logger_error(TAG, "Failed to stream file chunk");
            return ESP_FAIL;
        }
    }

    fclose(file);

    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static const httpd_uri_t static_file_uri =
{
    .uri = "/*",
    .method = HTTP_GET,
    .handler = static_file_handler,
    .user_ctx = NULL
};

/* Returns the HTTP server configuration. */
static httpd_config_t webserver_configure(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = WEBSERVER_PORT;
    config.max_uri_handlers = WEBSERVER_MAX_URI_HANDLERS;
    config.stack_size = 8192;
    config.recv_wait_timeout = 20;
    config.lru_purge_enable = true;

    // Enables wildcard matching for '/*' static routes
    config.uri_match_fn = httpd_uri_match_wildcard;

    return config;
}

/* Registers all HTTP endpoints. */
static esp_err_t webserver_register_routes(void)
{
    esp_err_t err;

    // 1. Register REST API routes from components first
    err = routes_register(s_webserver.server);
    if (err != ESP_OK)
    {
        return err;
    }

    // 2. Register fallback wildcard route for static SPIFFS files
    return httpd_register_uri_handler(
        s_webserver.server,
        &static_file_uri);
}

/* Stops the server and clears the context. */
static void webserver_cleanup(void)
{
    if (s_webserver.server != NULL)
    {
        httpd_stop(
            s_webserver.server);

        s_webserver.server = NULL;
    }

    s_webserver.running = false;
}

/* Initializes SPIFFS and web server context. */
esp_err_t webserver_init(void)
{
    memset(&s_webserver, 0, sizeof(s_webserver));

    /* Create the persistent API credential before accepting HTTP requests. */
    esp_err_t err = api_auth_init();

    if (err != ESP_OK)
    {
        logger_error(TAG, "API authentication initialization failed (%s)",
            esp_err_to_name(err));
        return err;
    }

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        
        /* Do not erase the SPIFFS partition automatically when mounting fails. */
        .format_if_mount_failed = false
    };

    err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "Failed to mount SPIFFS (%s)",
            esp_err_to_name(err));
        return err;
    }

    logger_info(
        TAG,
        "SPIFFS mounted successfully at /spiffs");

    logger_info(
        TAG,
        "Web server initialized");

    return ESP_OK;
}

/* Starts the HTTP server. */
esp_err_t webserver_start(void)
{
    httpd_config_t config = webserver_configure();
    esp_err_t err;

    if (s_webserver.running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    err = httpd_start(
        &s_webserver.server,
        &config);

    if (!wifi_manager_is_connected())
    {
        logger_warn(
            TAG,
            "Starting HTTP server without WiFi connection");
    }

    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "HTTP server start failed: %s",
            esp_err_to_name(err));

        return err;
    }

    err = webserver_register_routes();
    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "Route registration failed (%s)",
            esp_err_to_name(err));

        webserver_cleanup();
        return err;
    }

    s_webserver.running = true;

    logger_info(
        TAG,
        "HTTP server started on port %u",
        config.server_port);

    return ESP_OK;
}

/* Stops the HTTP server. */
esp_err_t webserver_stop(void)
{
    webserver_cleanup();

    logger_info(
        TAG,
        "HTTP server stopped");

    return ESP_OK;
}

/* Returns whether the server is running. */
bool webserver_is_running(void)
{
    return s_webserver.running;
}
