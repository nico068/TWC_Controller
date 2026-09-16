#include "api_system.h"
#include "api.h"
#include "json_utils.h"
#include <stdio.h>
#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_http_server.h"

#include "config.h"
#include "wifi_manager.h"
#include "rs485.h"

/*
 * System information endpoint that reports firmware, hardware and connectivity data.
 */

typedef struct
{
    esp_chip_info_t chip;

    uint8_t mac[6];

    uint64_t uptime_ms;

} api_system_info_t;

/* Retrieves the chip information. */
static void api_system_get_chip_info(
    esp_chip_info_t *chip_info);

/* Retrieves the chip information. */
static void api_system_get_chip_info(
    esp_chip_info_t *chip_info)
{
    esp_chip_info(
        chip_info);
}
/* Retrieves the WiFi station MAC address. */
static void api_system_get_mac(
    uint8_t mac[6]);

/* Retrieves the WiFi station MAC address. */
static void api_system_get_mac(
    uint8_t mac[6])
{
    esp_read_mac(
        mac,
        ESP_MAC_WIFI_STA);
}

/* Collects all runtime system information. */
static void api_system_collect_info(
    api_system_info_t *info);

/* Collects all runtime system information. */
static void api_system_collect_info(
    api_system_info_t *info)
{
    api_system_get_chip_info(
        &info->chip);

    api_system_get_mac(
        info->mac);

    info->uptime_ms =
        (uint64_t)(esp_timer_get_time() / 1000ULL);
}

/* Writes the system information as JSON. */
static void api_system_write_json(
    json_writer_t *json,
    const system_config_t *cfg,
    const wifi_manager_status_t *wifi,
    const api_system_info_t *info);

/* Writes the system information as JSON. */
static void api_system_write_json(
    json_writer_t *json,
    const system_config_t *cfg,
    const wifi_manager_status_t *wifi,
    const api_system_info_t *info)
{
    json_write_string(
        json,
        "hostname",
        cfg->hostname);

    json_write_string(
        json,
        "idf",
        esp_get_idf_version());

    json_write_string(
        json,
        "firmware",
        APP_VERSION);

    json_write_int(
        json,
        "chip_cores",
        info->chip.cores);

    json_write_int(
        json,
        "chip_revision",
        info->chip.revision);

    json_write_uint(
        json,
        "free_heap",
        esp_get_free_heap_size());

    json_write_uint(
        json,
        "minimum_heap",
        esp_get_minimum_free_heap_size());

    json_write_int(
        json,
        "cpu_freq_mhz",
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    json_write_uint64(
        json,
        "uptime_ms",
        info->uptime_ms);

    json_write_bool(
        json,
        "wifi_connected",
        wifi->got_ip);

    json_write_int(
        json,
        "wifi_state",
        wifi->state);

    json_write_ip(
        json,
        "ip",
        &wifi->ip.ip);

    json_write_bool(
        json,
        "wall_connector_connected",
        rs485_wall_connector_is_connected());

    json_write_mac(
        json,
        "mac",
        info->mac);
}

/* Handles GET /api/system requests. */
static esp_err_t api_system_get_handler(httpd_req_t *req)
{
	if (!api_require_auth(req))
	{
		return ESP_OK;
	}

	const system_config_t *cfg =
        config_get();

    const wifi_manager_status_t *wifi =
        wifi_manager_get_status();

    api_system_info_t info;

    api_system_collect_info(
        &info);

    char response[512];

    json_writer_t json;

    json_init(
        &json,
        response,
        sizeof(response));

    json_begin_object(
        &json);

    api_system_write_json(
        &json,
        cfg,
        wifi,
        &info);

    json_end_object(
        &json);

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

/* GET /api/system endpoint descriptor. */
static const httpd_uri_t system_get_uri =
{
	.uri = "/api/system",
	.method = HTTP_GET,
	.handler = api_system_get_handler,
	.user_ctx = NULL
};

/* Registers the system information endpoint. */
esp_err_t api_system_register(httpd_handle_t server)
{
	return httpd_register_uri_handler(
		server,
		&system_get_uri);
}
