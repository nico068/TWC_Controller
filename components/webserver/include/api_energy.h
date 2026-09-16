#ifndef API_ENERGY_H
#define API_ENERGY_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the energy status endpoint. */
esp_err_t api_energy_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif
