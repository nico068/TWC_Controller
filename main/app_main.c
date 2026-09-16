#include "esp_ota_ops.h"

/*
 * Application entry point for the TWC Controller firmware.
 * It initializes persistent storage, loads the system configuration,
 * starts the WiFi manager, launches the embedded web server and scheduler,
 * and then exits the startup task.
 */
#include "logger.h"
#include "storage.h"
#include "config.h"
#include "watchdog.h"

#include "power_manager.h"

#include "rs485.h"

#include "wifi_manager.h"

#include "shelly.h"

#include "webserver.h"

#include "scheduler.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_netif.h"

void app_main(void)
{
    /* Core services */
    logger_init();

    logger_info(
        "APP",
        "Application starting");

    if (!storage_init())
    {
        logger_info(
            "APP",
            "Storage initialization failed");
        return;
    }
    config_init();

    watchdog_init();   
    
    const system_config_t *cfg =
        config_get();

    logger_info(
        "APP",
        "SSID = '%s'",
        cfg->wifi_ssid);

    logger_info(
        "APP",
        "PASS = ********");
    
    /* ESP-IDF */
    ESP_ERROR_CHECK(esp_netif_init());

    /*
     * Create the default system event loop before the WiFi manager registers
     * its WIFI_EVENT and IP_EVENT handlers. The removed legacy event manager
     * previously performed this initialization implicitly.
     */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Power manager */
    ESP_ERROR_CHECK(power_manager_init());
    
    /* RS485 */
    ESP_ERROR_CHECK(rs485_init());
    ESP_ERROR_CHECK(rs485_start());

    /* Report the configured fail-safe current instead of a hard-coded value. */
    logger_info(
        "APP",
        "Active Neurio Modbus emulator initialized "
        "(slave 1, 115200 8N1, fail-safe %uA)",
        (unsigned)config_get()->breaker_limit);

    /* Network */
    ESP_ERROR_CHECK(wifi_manager_init());
    esp_err_t err = wifi_manager_start();

    if (err != ESP_OK)
    {
        logger_warn(
            "APP",
            "WiFi start failed: %s",
            esp_err_to_name(err));
    }

    /* Shelly */
    ESP_ERROR_CHECK(shelly_init());
    ESP_ERROR_CHECK(shelly_start());

    /* The measurement mutex must exist before the power manager reads it. */
    ESP_ERROR_CHECK(power_manager_start());
    
    /* HTTP */
    ESP_ERROR_CHECK(webserver_init());
    ESP_ERROR_CHECK(webserver_start());

    /* Scheduler */
    ESP_ERROR_CHECK(scheduler_init());
    ESP_ERROR_CHECK(scheduler_start());
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
#endif

    vTaskDelete(NULL);
}
