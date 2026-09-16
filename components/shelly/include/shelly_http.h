#ifndef SHELLY_HTTP_H
#define SHELLY_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Performs an HTTP GET request. */
bool shelly_http_get(
    const char *uri,
    char *response,
    size_t response_size);

/* Performs an HTTP POST request. */
bool shelly_http_post(
    const char *uri,
    const char *body,
    char *response,
    size_t response_size);

#ifdef __cplusplus
}
#endif

#endif