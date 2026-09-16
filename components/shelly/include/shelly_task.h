#ifndef SHELLY_TASK_H
#define SHELLY_TASK_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the Shelly polling task control state. */
esp_err_t shelly_task_init(void);

esp_err_t shelly_task_start(void);

esp_err_t shelly_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif