#ifndef WEBSERVER_ROUTES_H
#define WEBSERVER_ROUTES_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers all HTTP routes. */
esp_err_t routes_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif