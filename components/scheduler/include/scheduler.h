#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "esp_err.h"

/*
 * Scheduler API used to initialize and run background supervision tasks.
 */

esp_err_t scheduler_init(void);

esp_err_t scheduler_start(void);

#endif