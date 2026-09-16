#ifndef API_SHELLY_H
#define API_SHELLY_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t api_shelly_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif
