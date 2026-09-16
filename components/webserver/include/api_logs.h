#ifndef API_LOGS_H
#define API_LOGS_H

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Endpoint exposing the device log buffer over HTTP.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the log endpoints. */
esp_err_t api_logs_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif /* API_LOGS_H */