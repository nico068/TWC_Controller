#include "shelly_config.h"
#include "config.h"

/* Shelly host. */
static const char *s_host =
    SHELLY_DEFAULT_HOST;

/* Shelly HTTP port. */
static uint16_t s_port =
    SHELLY_DEFAULT_PORT;

/* HTTP timeout (ms). */
static uint32_t s_timeout =
    SHELLY_DEFAULT_TIMEOUT_MS;

/* HTTP username. */
static const char *s_username =
    "";

/* HTTP password. */
static const char *s_password =
    "";

const char *shelly_config_get_host(void)
{
    /* Configuration saved from the web interface takes precedence. */
    const char *configured = config_get()->shelly_host;
    return configured[0] != '\0' ? configured : s_host;
}

uint16_t shelly_config_get_port(void)
{
    return s_port;
}

uint32_t shelly_config_get_timeout(void)
{
    return s_timeout;
}

const char *shelly_config_get_username(void)
{
    return s_username;
}

const char *shelly_config_get_password(void)
{
    return s_password;
}
