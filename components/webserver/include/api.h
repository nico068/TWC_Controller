#ifndef API_H
#define API_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

/*
 * Shared helpers used by API endpoint handlers to send JSON responses.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Loads or creates the persistent administrator token. */
esp_err_t api_auth_init(void);

/* Verifies the HTTP Authorization: Bearer header and sends 401 on failure. */
bool api_require_auth(
    httpd_req_t *req);

/* Sends a JSON response. */
esp_err_t api_send_json(
    httpd_req_t *req,
    const char *json);

/* Sends a success response. */
esp_err_t api_send_ok(
    httpd_req_t *req);

/* Sends an HTTP error response. */
esp_err_t api_send_error(
    httpd_req_t *req,
    httpd_err_code_t code,
    const char *message);

#ifdef __cplusplus
}
#endif

#endif
