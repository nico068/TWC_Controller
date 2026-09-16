/* Serializes the controller configuration into a JSON payload for the API. */

#include "api.h"
#include "json_utils.h"
#include "logger.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "shelly.h"
#include "shelly_measurement.h"
#include "power_manager.h"
#include "shelly_config.h"
#include "config.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static void config_to_json(
    json_writer_t *json,
    const system_config_t *cfg)
{
    json_write_string(json, "hostname", cfg->hostname);

    json_write_string(json, "wifi_ssid", cfg->wifi_ssid);

    json_write_string(json, "wifi_password", "");

    json_write_string(json, "shelly_host", cfg->shelly_host);

    json_write_string(json, "shelly_active_host", shelly_config_get_host());

    json_write_uint(json, "breaker_limit", cfg->breaker_limit);
    json_write_uint(json, "shelly_phase", cfg->shelly_phase);

    json_write_uint(json, "charger_min_current", cfg->charger_min_current);

    json_write_uint(json, "charger_max_current", cfg->charger_max_current);

    json_write_bool(json, "ota_enabled", cfg->ota_enabled);
}

/* Handles GET /api/config requests. */
static esp_err_t api_config_get_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    const system_config_t *cfg = config_get();

    char response[512];

    json_writer_t json;

    json_init(&json, response, sizeof(response));

    json_begin_object(&json);

    config_to_json(&json, cfg);

    json_end_object(&json);

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

/* Receives the complete HTTP request body. */
static esp_err_t api_receive_body(
    httpd_req_t *req,
    char *buffer,
    size_t size)
{
    if ((req == NULL) ||
        (buffer == NULL) ||
        (size == 0))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((req->content_len == 0) ||
        (req->content_len >= size))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received =
        0;

    /* Limit consecutive socket timeouts while receiving the request body. */
    unsigned int timeout_count =
        0;

    /* Read the complete request body, which may arrive in several chunks. */
    while (received < req->content_len)
    {
        int len =
            httpd_req_recv(
                req,
                buffer + received,
                req->content_len - received);

        /* Retry the receive operation after a socket timeout. */
        if (len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            timeout_count++;
            if (timeout_count > 5)
            {
                return ESP_FAIL;
            }
            continue;
        }

        if (len <= 0)
        {
            return ESP_FAIL;
        }

        /* Reset the timeout counter after receiving data successfully. */
        timeout_count =
            0;

        received +=
            (size_t)len;
    }

    /* Terminate the received body as a C string. */
    buffer[received] =
        '\0';

    return ESP_OK;
}

/* Parses the request body as JSON. */
static cJSON *api_parse_json(httpd_req_t *req, const char *body)
{
    cJSON *root = cJSON_Parse(body);

    if (root == NULL)
    {
        (void)api_send_error(
            req,
            HTTPD_400_BAD_REQUEST,
            "invalid json");

        return NULL;
    }

    /* Configuration requests must contain a JSON object. */
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(
            root);

        (void)api_send_error(
            req,
            HTTPD_400_BAD_REQUEST,
            "json object required");

        return NULL;
    }

    return root;
}

/* Updates the configuration from a JSON document. */
static bool api_update_config(
    system_config_t *cfg,
    cJSON *root)
{
    cJSON *item;

    item = cJSON_GetObjectItem(root, "shelly_phase");
    if (item != NULL)
    {
        if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
            item->valuedouble < 0 || item->valuedouble > 3 ||
            floor(item->valuedouble) != item->valuedouble)
            return false;
        cfg->shelly_phase = (uint8_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(
        root,
        "breaker_limit");

    if (item != NULL)
    {
        /* Reject an invalid breaker limit before converting it to uint16_t. */
        if (!cJSON_IsNumber(item) ||
            !isfinite(item->valuedouble) ||
            (item->valuedouble < 0.0) ||
            (item->valuedouble > UINT16_MAX) ||
            (floor(item->valuedouble) !=
                item->valuedouble))
        {
            return false;
        }

        /* Store the validated breaker limit. */
        cfg->breaker_limit =
            (uint16_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(
        root,
        "charger_min_current");

    if (item != NULL)
    {
        /* Reject an invalid minimum charger current before converting it to uint16_t. */
        if (!cJSON_IsNumber(item) ||
            !isfinite(item->valuedouble) ||
            (item->valuedouble < 0.0) ||
            (item->valuedouble > UINT16_MAX) ||
            (floor(item->valuedouble) !=
                item->valuedouble))
        {
            return false;
        }

        /* Store the validated minimum charger current. */
        cfg->charger_min_current =
            (uint16_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(
        root,
        "charger_max_current");

    if (item != NULL)
    {
        /* Reject an invalid maximum charger current before converting it to uint16_t. */
        if (!cJSON_IsNumber(item) ||
            !isfinite(item->valuedouble) ||
            (item->valuedouble < 0.0) ||
            (item->valuedouble > UINT16_MAX) ||
            (floor(item->valuedouble) !=
                item->valuedouble))
        {
            return false;
        }

        /* Store the validated maximum charger current. */
        cfg->charger_max_current =
            (uint16_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(
        root,
        "ota_enabled");

    if (item != NULL)
    {
        /* Reject an invalid OTA enabled value. */
        if (!cJSON_IsBool(item))
        {
            return false;
        }

        /* Store the validated OTA enabled state. */
        cfg->ota_enabled =
            cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItem(
        root,
        "hostname");

    if (item != NULL)
    {
        /* Reject an invalid hostname value. */
        if (!cJSON_IsString(item) ||
            (item->valuestring == NULL))
        {
            return false;
        }

        /* Reject a hostname that does not fit in the destination buffer. */
        if (strlen(item->valuestring) >=
            sizeof(cfg->hostname))
        {
            return false;
        }

        /* Store the validated hostname. */
        strlcpy(
            cfg->hostname,
            item->valuestring,
            sizeof(cfg->hostname));
    }

    item = cJSON_GetObjectItem(
        root,
        "wifi_ssid");

    if (item != NULL)
    {
        /* Reject an invalid Wi-Fi SSID value. */
        if (!cJSON_IsString(item) ||
            (item->valuestring == NULL))
        {
            return false;
        }

        /* Reject a Wi-Fi SSID that does not fit in the destination buffer. */
        if (strlen(item->valuestring) >=
            sizeof(cfg->wifi_ssid))
        {
            return false;
        }

        /* Store the validated Wi-Fi SSID. */
        strlcpy(
            cfg->wifi_ssid,
            item->valuestring,
            sizeof(cfg->wifi_ssid));
    }

    item = cJSON_GetObjectItem(
        root,
        "wifi_password");

    if (item != NULL)
    {
        /* Reject an invalid Wi-Fi password value. */
        if (!cJSON_IsString(item) ||
            (item->valuestring == NULL))
        {
            return false;
        }

        /* Reject a Wi-Fi password that does not fit in the destination buffer. */
        if (strlen(item->valuestring) >=
            sizeof(cfg->wifi_password))
        {
            return false;
        }

        /* Store the validated Wi-Fi password. */
        strlcpy(
            cfg->wifi_password,
            item->valuestring,
            sizeof(cfg->wifi_password));
    }

    item = cJSON_GetObjectItem(
    root,
    "shelly_host");

    if (item != NULL)
    {
        /* Reject an invalid Shelly host value. */
        if (!cJSON_IsString(item) ||
            (item->valuestring == NULL))
        {
            return false;
        }

        /* Reject a Shelly host that does not fit in the destination buffer. */
        if (strlen(item->valuestring) >=
            sizeof(cfg->shelly_host))
        {
            return false;
        }

        /* Store the validated Shelly host. */
        strlcpy(
            cfg->shelly_host,
            item->valuestring,
            sizeof(cfg->shelly_host));
    }

    /* The configuration update completed successfully. */
    return true;
}

/* Returns whether the WiFi configuration changed. */
static bool api_network_config_changed(
    const char *old_ssid,
    const char *old_password,
    const system_config_t *cfg)
{
    return
        (strcmp(old_ssid, cfg->wifi_ssid) != 0) ||
        (strcmp(old_password, cfg->wifi_password) != 0);
}

/* Returns whether the configuration changed. */
static bool api_config_changed(
    const system_config_t *old_cfg,
    const system_config_t *new_cfg)
{
    return
        (strcmp(old_cfg->hostname,
                new_cfg->hostname) != 0) ||

        (strcmp(old_cfg->wifi_ssid,
                new_cfg->wifi_ssid) != 0) ||

        (strcmp(old_cfg->wifi_password,
                new_cfg->wifi_password) != 0) ||

        (strcmp(old_cfg->shelly_host,
                new_cfg->shelly_host) != 0) ||

        (old_cfg->shelly_phase != new_cfg->shelly_phase) ||

        (old_cfg->breaker_limit !=
         new_cfg->breaker_limit) ||

        (old_cfg->charger_min_current !=
         new_cfg->charger_min_current) ||

        (old_cfg->charger_max_current !=
         new_cfg->charger_max_current) ||

        (old_cfg->ota_enabled !=
         new_cfg->ota_enabled);
}

/* Saves the configuration and reconnects WiFi if required. */
static void api_shelly_reconfigure_task(void *arg)
{
    (void)arg;

    /* Detection uses HTTP and a 2 KB buffer: never run it on the HTTP task. */
    if (shelly_stop() != ESP_OK)
    {
        logger_error("API", "Shelly stop failed after host change");
        vTaskDelete(NULL);
        return;
    }

    /* Let the POST handler finish its optional WiFi reconnect first. */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* The Shelly task retries detection itself until WiFi and the host return. */
    if (shelly_start() != ESP_OK)
    {
        logger_warn("API", "Unable to restart Shelly task");
    }

    vTaskDelete(NULL);
}

static bool api_save_config(
    const system_config_t *old_cfg,
    const system_config_t *new_cfg,
    const char *old_ssid,
    const char *old_password)
{
    /* Preserve the complete previous configuration before modifying global state. */
    system_config_t previous_cfg =
        *old_cfg;

    /* Preserve the previous Shelly host before updating the global configuration. */
    char old_shelly_host[sizeof(old_cfg->shelly_host)];

    strlcpy(
        old_shelly_host,
        old_cfg->shelly_host,
        sizeof(old_shelly_host));

    if (!api_config_changed(
            old_cfg,
            new_cfg))
    {
        return true;
    }

    *config_get_mutable() = *new_cfg;

    if (!config_save())
    {
        /* Restore the previous configuration when persistence fails. */
        *config_get_mutable() =
            previous_cfg;

        logger_error(
            "API",
            "Configuration save failed");

        return false;
    }

    logger_info(
        "API",
        "Configuration updated");

    if (previous_cfg.shelly_phase != new_cfg->shelly_phase)
    {
        /* An old measurement must never be reused for a new phase mapping. */
        shelly_measurement_invalidate();
        power_manager_invalidate();
    }

    if (strcmp(
            old_shelly_host,
            new_cfg->shelly_host) != 0)
    {
        /* The HTTP server task is too small for Shelly HTTP detection. */
        if (xTaskCreate(api_shelly_reconfigure_task,
                        "shelly_config", 12288, NULL, 4, NULL) != pdPASS)
        {
            logger_error("API", "Unable to start Shelly reconfiguration");
            return false;
        }
    }

    if (api_network_config_changed(
            old_ssid,
            old_password,
            new_cfg))
    {
        logger_info(
            "API",
            "WiFi configuration changed");

        esp_err_t err =
            wifi_manager_reconnect();

        if (err != ESP_OK)
        {
            logger_warn(
                "API",
                "WiFi reconnect failed (%s)",
                esp_err_to_name(err));
        }
    }

    return true;
}

/* Handles POST /api/config requests. */
static esp_err_t api_config_post_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    char body[512];

    if (api_receive_body(
            req,
            body,
            sizeof(body)) != ESP_OK)
    {
        return api_send_error(
            req,
            HTTPD_400_BAD_REQUEST,
            "empty request");
    }

    cJSON *root = api_parse_json(req, body);

    if (root == NULL)
    {
        return ESP_OK;
    }

    const system_config_t *current_cfg =
        config_get();

    system_config_t new_cfg = *current_cfg;

    char old_ssid[sizeof(current_cfg->wifi_ssid)];
    char old_password[sizeof(current_cfg->wifi_password)];

    strlcpy(
        old_ssid,
        current_cfg->wifi_ssid,
        sizeof(old_ssid));

    strlcpy(
        old_password,
        current_cfg->wifi_password,
        sizeof(old_password));

    /* Reject malformed configuration values. */
    if (!api_update_config(
            &new_cfg,
            root))
    {
        cJSON_Delete(
            root);

        return api_send_error(
            req,
            HTTPD_400_BAD_REQUEST,
            "invalid configuration value");
    }

    /* Validate the resulting configuration. */
    if (!config_validate(
            &new_cfg))
    {
        cJSON_Delete(
            root);

        return api_send_error(
            req,
            HTTPD_400_BAD_REQUEST,
            "invalid configuration");
    }

    if (!api_save_config(
        current_cfg,
        &new_cfg,
        old_ssid,
        old_password))
    {
        cJSON_Delete(root);

        return api_send_error(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "configuration apply failed");
    }

    cJSON_Delete(root);

    return api_send_ok(req);
}

/* GET /api/config endpoint. */
static const httpd_uri_t config_get_uri =
{
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = api_config_get_handler,
    .user_ctx = NULL
};

/* POST /api/config endpoint. */
static const httpd_uri_t config_post_uri =
{
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = api_config_post_handler,
    .user_ctx = NULL
};

/* Registers the configuration endpoints. */
esp_err_t api_config_register(httpd_handle_t server)
{
    esp_err_t err;

    err = httpd_register_uri_handler(
    server,
    &config_get_uri);

    if (err != ESP_OK)
    {
        return err;
    }

    err = httpd_register_uri_handler(
    server,
    &config_post_uri);

    return err;
}
