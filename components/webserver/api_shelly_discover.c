#include "api_shelly_discover.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "api.h"
#include "cJSON.h"
#include "esp_netif.h"
#include "mdns.h"
#include "wifi_manager.h"

#define DISCOVERY_RESULTS 12
#define DISCOVERY_TIMEOUT_MS 1500

static bool s_mdns_initialized;

static bool shelly_name(const char *name)
{
    return name && strncasecmp(name, "shelly", 6) == 0;
}

static bool add_results(cJSON *list, mdns_result_t *results, bool shelly_service)
{
    for (mdns_result_t *result = results; result; result = result->next)
    {
        if (!shelly_service && !shelly_name(result->hostname) &&
            !shelly_name(result->instance_name))
        {
            continue;
        }

        char address[16] = {0};
        for (mdns_ip_addr_t *ip = result->addr; ip; ip = ip->next)
        {
            if (ip->addr.type == ESP_IPADDR_TYPE_V4)
            {
                snprintf(address, sizeof(address), IPSTR,
                         IP2STR(&ip->addr.u_addr.ip4));
                break;
            }
        }
        if (!address[0])
        {
            continue;
        }

        bool duplicate = false;
        cJSON *existing;
        cJSON_ArrayForEach(existing, list)
        {
            cJSON *saved = cJSON_GetObjectItemCaseSensitive(existing, "address");
            if (cJSON_IsString(saved) && !strcmp(saved->valuestring, address))
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        cJSON *item = cJSON_CreateObject();
        const char *name = result->instance_name ? result->instance_name :
                           (result->hostname ? result->hostname : "Shelly");
        if (!item ||
            !cJSON_AddStringToObject(item, "name", name) ||
            !cJSON_AddStringToObject(item, "address", address) ||
            !cJSON_AddItemToArray(list, item))
        {
            cJSON_Delete(item);
            return false;
        }
    }
    return true;
}

static esp_err_t discover_get(httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    if (!wifi_manager_is_connected())
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Wi-Fi indisponible", HTTPD_RESP_USE_STRLEN);
    }
    if (!s_mdns_initialized)
    {
        if (mdns_init() != ESP_OK)
        {
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_send(req, "mDNS indisponible", HTTPD_RESP_USE_STRLEN);
        }
        s_mdns_initialized = true;
    }

    cJSON *list = cJSON_CreateArray();
    if (!list)
    {
        return api_send_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    const char *services[] = {"_shelly", "_http"};
    bool success = true;
    for (size_t i = 0; i < 2 && success; i++)
    {
        mdns_result_t *results = NULL;
        esp_err_t err = mdns_query_ptr(services[i], "_tcp",
                                       DISCOVERY_TIMEOUT_MS, DISCOVERY_RESULTS, &results);
        if (err == ESP_OK)
        {
            success = add_results(list, results, i == 0);
        }
        if (results) mdns_query_results_free(results);
    }
    if (!success)
    {
        cJSON_Delete(list);
        return api_send_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    char *json = cJSON_PrintUnformatted(list);
    cJSON_Delete(list);
    if (!json)
    {
        return api_send_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    esp_err_t response = api_send_json(req, json);
    cJSON_free(json);
    return response;
}

static const httpd_uri_t discover_uri = {
    .uri = "/api/shelly/discover",
    .method = HTTP_GET,
    .handler = discover_get
};

esp_err_t api_shelly_discover_register(httpd_handle_t server)
{
    return httpd_register_uri_handler(server, &discover_uri);
}
