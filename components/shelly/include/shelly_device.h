#ifndef SHELLY_DEVICE_H
#define SHELLY_DEVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Supported Shelly generations. */
typedef enum
{
    SHELLY_GENERATION_UNKNOWN = 0,

    SHELLY_GENERATION_GEN1,

    SHELLY_GENERATION_GEN2_V1,

    SHELLY_GENERATION_GEN2_V2

} shelly_generation_t;

/* Snapshot of the detected Shelly device information. */
typedef struct
{
    /* Detected Shelly generation. */
    shelly_generation_t generation;

    /* Detected Shelly model name. */
    char model[32];

    /* Detected Shelly firmware version. */
    char firmware[32];

} shelly_device_info_t;

/* Initializes the Shelly device module. */
bool shelly_device_init(void);

/* Detects the connected Shelly device. */
bool shelly_device_detect(void);

/* Returns a consistent snapshot of the detected Shelly device information. */
bool shelly_device_get_info(
    shelly_device_info_t *info);

#ifdef __cplusplus
}
#endif

#endif