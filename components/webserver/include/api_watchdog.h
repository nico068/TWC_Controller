#ifndef API_WATCHDOG_H
#define API_WATCHDOG_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the watchdog health endpoint. */
esp_err_t api_watchdog_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif
