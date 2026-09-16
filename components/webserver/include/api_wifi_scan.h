#ifndef API_WIFI_SCAN_H
#define API_WIFI_SCAN_H

#include "esp_http_server.h"

esp_err_t api_wifi_scan_register(httpd_handle_t server);

#endif
