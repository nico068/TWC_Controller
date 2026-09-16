#ifndef SHELLY_JSON_H
#define SHELLY_JSON_H

#include <stdbool.h>

#include "shelly_measurement.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses a Shelly Gen1 measurement. */
bool shelly_json_parse_gen1_measurement(
    const char *json,
    shelly_measurement_t *measurement);

/* Parses a Shelly Gen2 V1 measurement. */
bool shelly_json_parse_gen2_v1_measurement(
    const char *json,
    shelly_measurement_t *measurement);

/* Parses a Shelly Gen2 V2 measurement. */
bool shelly_json_parse_gen2_v2_measurement(
    const char *json,
    shelly_measurement_t *measurement);

#ifdef __cplusplus
}
#endif

#endif