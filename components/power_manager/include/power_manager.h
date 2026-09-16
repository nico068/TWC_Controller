#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the power manager. */
esp_err_t power_manager_init(void);

/* Starts the power manager. */
esp_err_t power_manager_start(void);

/* Stops the power manager. */
esp_err_t power_manager_stop(void);

/* Immediately invalidates the current result and publishes a zero limit. */
void power_manager_invalidate(void);

/* Power manager runtime status. */
typedef struct
{
    /* Indicates whether the last Shelly measurement is valid. */
    bool measurement_valid;

    /* Last measured house current (A). */
    float house_current_a;

    /* Last measured house power (W). */
    float house_power_w;

    /* Available charging current (0.01 A). */
    uint16_t available_current_ca;

    /* Charging current limit (0.01 A). */
    uint16_t charge_limit_ca;

    /* Indicates whether charging is currently enabled. */
    bool charging_enabled;

} power_manager_status_t;

/* Returns the current power manager status. */
bool power_manager_get_status(
    power_manager_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
