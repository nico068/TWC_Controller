/*
 * Persistent system configuration values used by the controller.
 * This structure stores deployment parameters such as WiFi credentials,
 * charger limits and OTA settings.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * Configuration sizes
 ******************************************************************************/
#define CONFIG_WIFI_SSID_LENGTH        32U
#define CONFIG_WIFI_PASSWORD_LENGTH    64U
#define CONFIG_HOSTNAME_LENGTH         32U
#define CONFIG_SHELLY_HOST_LENGTH      64U

/******************************************************************************
 * Persistent configuration
 ******************************************************************************/
typedef struct
{
    char wifi_ssid[CONFIG_WIFI_SSID_LENGTH];

    char wifi_password[CONFIG_WIFI_PASSWORD_LENGTH];

    char hostname[CONFIG_HOSTNAME_LENGTH];

    char shelly_host[CONFIG_SHELLY_HOST_LENGTH];

    uint16_t breaker_limit;

    uint16_t charger_min_current;

    uint16_t charger_max_current;

    bool ota_enabled;

    /* 0: all phases (highest current); 1/2/3: Shelly A/B/C. */
    uint8_t shelly_phase;

} system_config_t;

/* Initializes the configuration subsystem. */
void config_init(void);

/* Restores the factory default configuration. */
void config_factory_reset(void);

/* Loads the configuration from persistent storage. */
bool config_load(void);

/* Saves the configuration to persistent storage. */
bool config_save(void);

/* Returns the current configuration. */
const system_config_t *config_get(void);

/* Returns a writable configuration instance. */
system_config_t *config_get_mutable(void);

/* Validates a configuration structure. */
bool config_validate(
    const system_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H_ */