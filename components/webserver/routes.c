#include "routes.h"

#include "api_status.h"
#include "api_config.h"
#include "api_logs.h"
#include "api_system.h"
#include "api_reboot.h"
#include "api_energy.h"
#include "api_watchdog.h"
#include "api_health.h"
#include "api_shelly.h"
#include "api_wifi_scan.h"
#include "api_shelly_discover.h"
#include "api_ota.h"

#define REGISTER_ROUTE(route)            \
    do                                   \
    {                                    \
        esp_err_t err = route(server);   \
        if (err != ESP_OK)               \
        {                                \
            return err;                  \
        }                                \
    } while (0)

/* Registers all available HTTP endpoints for the device management API. */
esp_err_t routes_register(httpd_handle_t server)
{
    REGISTER_ROUTE(api_status_register);
    REGISTER_ROUTE(api_system_register);
    REGISTER_ROUTE(api_config_register);
    REGISTER_ROUTE(api_wifi_scan_register);
    REGISTER_ROUTE(api_shelly_register);
    REGISTER_ROUTE(api_shelly_discover_register);
    REGISTER_ROUTE(api_ota_register);
    REGISTER_ROUTE(api_energy_register);
    REGISTER_ROUTE(api_health_register);
    REGISTER_ROUTE(api_watchdog_register);
    REGISTER_ROUTE(api_logs_register);    
    REGISTER_ROUTE(api_reboot_register);

    return ESP_OK;
}
