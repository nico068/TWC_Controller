#ifndef RS485_H
#define RS485_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the Gen 3 Neurio-compatible Modbus RTU interface. */
esp_err_t rs485_init(void);

/* Starts the Modbus request-processing task. */
esp_err_t rs485_start(void);

/* Stops the task and releases the UART resources. */
esp_err_t rs485_stop(void);

/* Returns true when the Gen 3 Wall Connector is actively polling Modbus. */
bool rs485_wall_connector_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
