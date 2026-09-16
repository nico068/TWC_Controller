#ifndef API_STATUS_H
#define API_STATUS_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the system status API. */
esp_err_t api_status_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif