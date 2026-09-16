/*
 * Runtime copy of the system configuration persisted in NVS.
 * It is loaded at startup and validated before use.
 */

#include "config.h"
#include "storage.h"

#include <string.h>

/* Default configuration values. */
#define CONFIG_DEFAULT_HOSTNAME           "twc-controller"
#define CONFIG_DEFAULT_WIFI_SSID          ""
#define CONFIG_DEFAULT_WIFI_PASSWORD      ""
#define CONFIG_DEFAULT_SHELLY_HOST        ""

#define CONFIG_DEFAULT_BREAKER_LIMIT      20U
#define CONFIG_DEFAULT_MIN_CURRENT        6U
#define CONFIG_DEFAULT_MAX_CURRENT        16U

#define CONFIG_DEFAULT_OTA_ENABLED        true

/* NVS keys used by the persistent configuration. */
#define CONFIG_KEY_SYSTEM                 "system"
#define CONFIG_KEY_SHELLY_PHASE           "shelly_phase"

static system_config_t s_config;

static bool config_is_valid_shelly_host(const char *host)
{
    if (host == NULL)
    {
        return false;
    }

    if (host[0] == '\0')
    {
        return true;
    }

    if ((strncmp(host, "http://", 7) == 0) ||
        (strncmp(host, "https://", 8) == 0))
    {
        host = strstr(host, "://") + 3;
    }
    else if (strstr(host, "://") != NULL)
    {
        return false;
    }

    if (host[0] == '\0')
    {
        return false;
    }

    if (strchr(host, '/') != NULL ||
        strchr(host, '?') != NULL ||
        strchr(host, '#') != NULL ||
        strchr(host, ' ') != NULL ||
        strchr(host, '\t') != NULL)
    {
        return false;
    }

    bool has_alnum = false;

    for (const char *p = host; *p != '\0'; ++p)
    {
        char c = *p;
        bool allowed =
            ((c >= 'a') && (c <= 'z')) ||
            ((c >= 'A') && (c <= 'Z')) ||
            ((c >= '0') && (c <= '9')) ||
            (c == '.') ||
            (c == '-') ||
            (c == ':') ||
            (c == '[') ||
            (c == ']');

        if (!allowed)
        {
            return false;
        }

        if (((c >= 'a') && (c <= 'z')) ||
            ((c >= 'A') && (c <= 'Z')) ||
            ((c >= '0') && (c <= '9')))
        {
            has_alnum = true;
        }
    }

    return has_alnum;
}

/* Fills a configuration structure with factory default values. */
static void config_set_defaults(system_config_t *cfg)
{
    memset(
        cfg,
        0,
        sizeof(*cfg));

    strlcpy(
        cfg->hostname,
        CONFIG_DEFAULT_HOSTNAME,
        sizeof(cfg->hostname));

    strlcpy(
        cfg->wifi_ssid,
        CONFIG_DEFAULT_WIFI_SSID,
        sizeof(cfg->wifi_ssid));

    strlcpy(
        cfg->wifi_password,
        CONFIG_DEFAULT_WIFI_PASSWORD,
        sizeof(cfg->wifi_password));

    cfg->shelly_host[0] = '\0';

    cfg->breaker_limit = CONFIG_DEFAULT_BREAKER_LIMIT;

    cfg->charger_min_current = CONFIG_DEFAULT_MIN_CURRENT;

    cfg->charger_max_current = CONFIG_DEFAULT_MAX_CURRENT;

    cfg->ota_enabled = CONFIG_DEFAULT_OTA_ENABLED;
    cfg->shelly_phase = 0;
}

/* Loads and validates the persisted configuration. */
static bool config_load_valid(void)
{
    if (!config_load())
    {
        return false;
    }

    return config_validate(
        &s_config);
}

/* Validates a configuration structure. */
bool config_validate(const system_config_t *cfg)
{
    if (cfg == NULL)
    {
        return false;
    }

    if (cfg->hostname[0] == '\0')
    {
        return false;
    }

    /* An empty SSID is valid only for the first-boot provisioning mode. */
    if ((cfg->wifi_ssid[0] == '\0') &&
        (cfg->wifi_password[0] != '\0'))
    {
        return false;
    }

    /* WPA/WPA2 personal passwords contain 8 to 63 characters. */
    size_t wifi_password_length =
        strlen(cfg->wifi_password);

    if ((wifi_password_length != 0U) &&
        ((wifi_password_length < 8U) ||
         (wifi_password_length > 63U)))
    {
        return false;
    }

    if (!config_is_valid_shelly_host(cfg->shelly_host))
    {
        return false;
    }

    if (cfg->shelly_phase > 3U)
    {
        return false;
    }

    if ((cfg->charger_min_current < 6U) ||
        (cfg->charger_min_current > 32U))
    {
        return false;
    }

    if ((cfg->charger_max_current < 6U) ||
        (cfg->charger_max_current > 32U))
    {
        return false;
    }

    if (cfg->charger_min_current >
        cfg->charger_max_current)
    {
        return false;
    }

    if ((cfg->breaker_limit < 6U) ||
        (cfg->breaker_limit > 63U))
    {
        return false;
    }

    if (cfg->breaker_limit <
        cfg->charger_max_current)
    {
        return false;
    }

    return true;
}

/* Restores the factory default configuration. */
void config_factory_reset(void)
{
    config_set_defaults(
        &s_config);
}

/* Restores the factory configuration and saves it. */
static void config_restore_defaults(void)
{
    config_factory_reset();

    config_save();
}

/* Loads the configuration from persistent storage. */
bool config_load(void)
{
    system_config_t loaded_config;

    /*
     * Load into a local snapshot. The global configuration is published only
     * after every persistent field has been resolved, so RS485 can never see
     * a temporary default phase during startup.
     */
    if (!storage_read(
            CONFIG_KEY_SYSTEM,
            &loaded_config,
            sizeof(loaded_config)))
    {
        return false;
    }

    uint8_t stored_phase;

    if (!storage_read(
            CONFIG_KEY_SHELLY_PHASE,
            &stored_phase,
            sizeof(stored_phase)))
    {
        /*
         * One-time migration for firmware versions that stored the phase only
         * inside the system blob.
         */
        stored_phase =
            (loaded_config.shelly_phase <= 3U)
                ? loaded_config.shelly_phase
                : 0U;

        if (!storage_write(
                CONFIG_KEY_SHELLY_PHASE,
                &stored_phase,
                sizeof(stored_phase)))
        {
            return false;
        }
    }

    /* Reject a corrupted persistent phase value. */
    if (stored_phase > 3U)
    {
        return false;
    }

    loaded_config.shelly_phase =
        stored_phase;

    /*
     * Publish the complete configuration only after the phase has been loaded.
     * No task can observe an intermediate triphase value.
     */
    s_config =
        loaded_config;

    return true;
}

/* Saves the configuration to persistent storage. */
bool config_save(void)
{
    const system_config_t snapshot =
        s_config;

    /* Save the complete system configuration first. */
    if (!storage_write(
            CONFIG_KEY_SYSTEM,
            &snapshot,
            sizeof(snapshot)))
    {
        return false;
    }

    /*
     * Save the phase in a dedicated key. This value remains authoritative
     * even if the system configuration structure changes in a future version.
     */
    if (!storage_write(
            CONFIG_KEY_SHELLY_PHASE,
            &snapshot.shelly_phase,
            sizeof(snapshot.shelly_phase)))
    {
        return false;
    }

    uint8_t verified_phase;

    /* Verify that the committed NVS value matches the requested phase. */
    if (!storage_read(
            CONFIG_KEY_SHELLY_PHASE,
            &verified_phase,
            sizeof(verified_phase)))
    {
        return false;
    }

    return verified_phase ==
        snapshot.shelly_phase;
}

/* Initializes the configuration subsystem. */
void config_init(void)
{
    if (!config_load_valid())
    {
        config_restore_defaults();
        return;
    }

}

/* Returns the current configuration. */
const system_config_t *config_get(void)
{
    return &s_config;
}

/* Returns a writable configuration instance. */
system_config_t *config_get_mutable(void)
{
    return &s_config;
}
