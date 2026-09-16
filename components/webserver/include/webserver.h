#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>

/*
 * Public API for the embedded HTTP server.
 * This layer exposes the lifecycle hooks used by the application startup code.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise le serveur HTTP */
esp_err_t webserver_init(void);

/* Démarre le serveur HTTP */
esp_err_t webserver_start(void);

/* Arrête le serveur HTTP */
esp_err_t webserver_stop(void);

/* Indique si le serveur est démarré */
bool webserver_is_running(void);

#ifdef __cplusplus
}
#endif

#endif