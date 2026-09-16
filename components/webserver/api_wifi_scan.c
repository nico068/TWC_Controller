#include "api_wifi_scan.h"

#include "api.h"
#include "wifi_manager.h"

#include "cJSON.h"

#define WIFI_SCAN_MAX_RESULTS 12

/* Returns nearby SSIDs so the settings page can populate its selector. */
static esp_err_t api_wifi_scan_get_handler(httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    wifi_manager_ap_t networks[WIFI_SCAN_MAX_RESULTS];
    size_t count = 0;

    esp_err_t err = wifi_manager_scan(
        networks, WIFI_SCAN_MAX_RESULTS, &count);
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "WiFi scan unavailable",
            HTTPD_RESP_USE_STRLEN);
    }

    cJSON *root = cJSON_CreateArray();
    if (root == NULL)
    {
        return api_send_error(req,
            HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (networks[i].ssid[0] == '\0')
        {
            continue;
        }

        cJSON *ap = cJSON_CreateObject();
        if ((ap == NULL) ||
            !cJSON_AddStringToObject(ap, "ssid", networks[i].ssid) ||
            !cJSON_AddNumberToObject(ap, "rssi", networks[i].rssi) ||
            !cJSON_AddBoolToObject(ap, "secured", networks[i].secured) ||
            !cJSON_AddItemToArray(root, ap))
        {
            cJSON_Delete(ap);
            cJSON_Delete(root);
            return api_send_error(req,
                HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return api_send_error(req,
            HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    esp_err_t response = api_send_json(req, json);
    cJSON_free(json);
    return response;
}

static const httpd_uri_t wifi_scan_uri = {
    .uri = "/api/wifi/scan",
    .method = HTTP_GET,
    .handler = api_wifi_scan_get_handler,
    .user_ctx = NULL
};

esp_err_t api_wifi_scan_register(httpd_handle_t server)
{
    return httpd_register_uri_handler(server, &wifi_scan_uri);
}
