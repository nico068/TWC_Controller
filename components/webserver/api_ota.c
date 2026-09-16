#include "api_ota.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "api.h"
#include "config.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define CHUNK_BYTES 1024

static void reboot_task(void *unused)
{
    (void)unused;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    vTaskDelete(NULL);
}

static esp_err_t ota_post(httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    if (!config_get()->ota_enabled)
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "OTA disabled");

    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!slot || req->content_len < 24 ||
        (size_t)req->content_len > slot->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware size");
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(slot, req->content_len, &handle);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    uint8_t chunk[CHUNK_BYTES];
    int remaining = req->content_len;
    int retries = 0;
    bool first = true;
    while (remaining > 0)
    {
        int limit = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
        int received = httpd_req_recv(req, (char *)chunk, limit);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && retries++ < 5) continue;
        if (received <= 0) { err = ESP_FAIL; break; }
        retries = 0;
        if (first && chunk[0] != 0xE9) { err = ESP_FAIL; break; }
        first = false;
        err = esp_ota_write(handle, chunk, received);
        if (err != ESP_OK) break;
        remaining -= received;
    }
    if (err == ESP_OK && remaining == 0) err = esp_ota_end(handle);
    else esp_ota_abort(handle);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or incomplete firmware");
    esp_err_t sent = api_send_json(req, "{\"status\":\"rebooting\"}");
    if (xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL) != pdPASS)
        esp_restart();
    return sent;
}
static const httpd_uri_t ota_uri = {
    .uri = "/api/ota/firmware", .method = HTTP_POST, .handler = ota_post
};
esp_err_t api_ota_register(httpd_handle_t server)
{
    return httpd_register_uri_handler(server, &ota_uri);
}
