/*
 * High-level WiFi state exposed to the rest of the application.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
    WIFI_MANAGER_DISCONNECTED = 0,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_CONNECTED

} wifi_manager_state_t;

/*
 * Current connection status reported by the WiFi manager.
 * It includes the connection state and IP information once a station gets an address.
 */
typedef struct
{
    wifi_manager_state_t state;

    bool got_ip;

    esp_netif_ip_info_t ip;

} wifi_manager_status_t;

/* One access point returned by an on-demand station scan. */
typedef struct
{
    char ssid[33];
    int8_t rssi;
    bool secured;
} wifi_manager_ap_t;

/******************************************************************************
 * Lifecycle management
 ******************************************************************************/

/* Initializes the WiFi manager and registers the underlying ESP-IDF event hooks. */
esp_err_t wifi_manager_init(void);

/* Starts the WiFi station using the configured network credentials. */
esp_err_t wifi_manager_start(void);

/* Stops the station and resets the connection state. */
esp_err_t wifi_manager_stop(void);

/* Releases the manager and unregisters all ESP-IDF event handlers. */
esp_err_t wifi_manager_deinit(void);

/* Forces a connection retry after a disconnect to re-establish the session. */
esp_err_t wifi_manager_reconnect(void);

/* Scan nearby networks; a failed scan leaves count at zero. */
esp_err_t wifi_manager_scan(
    wifi_manager_ap_t *access_points,
    size_t capacity,
    size_t *count);

/******************************************************************************
 * Status accessors
 ******************************************************************************/

/* Returns true when the station has a valid IP address. */
bool wifi_manager_is_connected(void);

/* Returns the current WiFi connection state. */
wifi_manager_state_t wifi_manager_get_state(void);

/* Returns the complete WiFi manager status. */
const wifi_manager_status_t *wifi_manager_get_status(void);

/* Returns the current IP information associated with the active WiFi interface. */
const esp_netif_ip_info_t *wifi_manager_get_ip(void);

#ifdef __cplusplus
}
#endif

#endif
