#ifndef SHELLY_MEASUREMENT_H
#define SHELLY_MEASUREMENT_H

#include <stdbool.h>

/* Number of electrical phases supported by the measurement snapshot. */
#define SHELLY_PHASE_COUNT 3U

/* Shelly electrical measurements. */
typedef struct
{
    /* Active power (W). */
    float power;
    /* RMS voltage (V). */
    float voltage;
    /* RMS current (A). */
    float current;
    /* Imported energy (Wh). */
    float energy;

    /*
    * Individual phase measurements.
    *
    * Index 0 corresponds to Shelly phase A.
    * Index 1 corresponds to Shelly phase B.
    * Index 2 corresponds to Shelly phase C.
    *
    * The legacy power, voltage and current fields above are retained because
    * power_manager currently consumes their aggregated/selected values.
    */
    float phase_power[SHELLY_PHASE_COUNT];
    float phase_voltage[SHELLY_PHASE_COUNT];
    float phase_current[SHELLY_PHASE_COUNT];

} shelly_measurement_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the Shelly measurement module. */
bool shelly_measurement_init(void);

/* Updates electrical measurements from the Shelly. */
bool shelly_measurement_update(void);

/* Returns a consistent snapshot of the latest Shelly measurement. */
bool shelly_measurement_get(
    shelly_measurement_t *measurement);

/* Mark the last reading unusable after polling stops or its source changes. */
void shelly_measurement_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif
