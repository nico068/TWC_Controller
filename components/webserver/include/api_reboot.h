#ifndef API_REBOOT_H
#define API_REBOOT_H

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Endpoint used to request a clean system reboot.
 */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t api_reboot_register(
    httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif