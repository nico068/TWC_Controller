#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * System information endpoint declaration.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the system information endpoint. */
esp_err_t api_system_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif