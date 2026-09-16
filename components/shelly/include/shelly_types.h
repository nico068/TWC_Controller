#ifndef SHELLY_TYPES_H
#define SHELLY_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Latest measurements received from the Shelly meter. */
typedef struct
{
    /* Indicates whether the measurements are valid. */
    bool valid;

    /* Power imported from the grid (W). */
    int32_t grid_power;

    /* Total house consumption (W). */
    int32_t house_power;

    /* Solar production (W). */
    int32_t solar_power;

    /* RMS line voltage (V). */
    float voltage;

    /* RMS current (A). */
    float current;

} shelly_measurements_t;

#ifdef __cplusplus
}
#endif

#endif