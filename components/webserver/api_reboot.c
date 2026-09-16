#include "api_reboot.h"
#include "api.h"

#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Restarts the device after a short delay. */
static void reboot_task(
    void *arg)
{
    (void)arg;

    vTaskDelay(
        pdMS_TO_TICKS(500));

    esp_restart();
}

/* Creates the delayed reboot task. */
static esp_err_t api_reboot_start_task(void)
{
    BaseType_t result =
        xTaskCreate(
            reboot_task,
            "reboot",
            2048,
            NULL,
            5,
            NULL);

    return (result == pdPASS)
        ? ESP_OK
        : ESP_FAIL;
}

/* Handles POST /api/reboot requests. */
static esp_err_t api_reboot_post_handler(
    httpd_req_t *req);

/* Handles POST /api/reboot requests. */
static esp_err_t api_reboot_post_handler(
    httpd_req_t *req)
{
    if (!api_require_auth(req))
    {
        return ESP_OK;
    }

    httpd_resp_set_type(
        req,
        "application/json");

    esp_err_t err =
        httpd_resp_sendstr(
            req,
            "{\"status\":\"rebooting\"}");

    if (err != ESP_OK)
    {
        return err;
    }

    err = api_reboot_start_task();

    if (err != ESP_OK)
    {
        return err;
    }

    return ESP_OK;
}

/* Reboot endpoint descriptor. */
static const httpd_uri_t reboot_uri =
{
    .uri      = "/api/reboot",
    .method   = HTTP_POST,
    .handler  = api_reboot_post_handler,
    .user_ctx = NULL
};

/* Registers the reboot endpoint. */
esp_err_t api_reboot_register(
    httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &reboot_uri);
}
