#ifndef SHELLY_H
#define SHELLY_H

#include "esp_err.h"

#include "shelly_device.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t shelly_init(void);

esp_err_t shelly_start(void);

esp_err_t shelly_stop(void);

#ifdef __cplusplus
}
#endif

#endif