#ifndef SHELLY_CONFIG_H
#define SHELLY_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No hidden fallback host: the web configuration is authoritative. */
#define SHELLY_DEFAULT_HOST              ""

/* Default Shelly HTTP port. */
#define SHELLY_DEFAULT_PORT              80

/* Default HTTP timeout (ms). */
#define SHELLY_DEFAULT_TIMEOUT_MS        3000

/* Returns the configured Shelly host. */
const char *shelly_config_get_host(void);

/* Returns the configured Shelly port. */
uint16_t shelly_config_get_port(void);

/* Returns the HTTP timeout. */
uint32_t shelly_config_get_timeout(void);

/* Returns the configured username. */
const char *shelly_config_get_username(void);

/* Returns the configured password. */
const char *shelly_config_get_password(void);

#ifdef __cplusplus
}
#endif

#endif
