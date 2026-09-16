/*
 * Internal runtime state shared by the WiFi manager.
 * It keeps track of initialization, connection attempts, events,
 * the ESP-IDF network interface and the current status reported to callers.
 */

#include "wifi_manager.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "logger.h"

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#define TAG "WIFI"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_MAXIMUM_RETRY 10
#define WIFI_SETUP_PASSWORD_LENGTH 12U

typedef struct
{
    bool initialized;
    bool started;

    uint8_t retry_count;

    SemaphoreHandle_t mutex;

    EventGroupHandle_t event_group;

    esp_netif_t *netif;

    esp_netif_t *ap_netif;

    bool provisioning;

    esp_event_handler_instance_t wifi_handler;

    esp_event_handler_instance_t ip_handler;

    wifi_manager_status_t status;

} wifi_manager_context_t;

/*
 * Single instance of the manager state.
 * This keeps the connection lifecycle and event-driven transitions centralized.
 */
static wifi_manager_context_t s_wifi =
{
    .initialized = false,
    .started = false,
    .retry_count = 0,
    .mutex = NULL,
    .event_group = NULL,
    .netif = NULL
};

/******************************************************************************
 * Private functions
 ******************************************************************************/

static void wifi_lock(void);

static void wifi_unlock(void);

static void wifi_handle_start(void);

static void wifi_handle_disconnect(
    wifi_event_sta_disconnected_t *event);

static void wifi_handle_got_ip(
    ip_event_got_ip_t *event);

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data);

static void ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data);

static void wifi_scan_access_points(void);

static void wifi_unregister_handlers(void);

static void wifi_cleanup(void);

static esp_err_t wifi_create_netif(void);

static esp_err_t wifi_create_ap_netif(void);

static esp_err_t wifi_init_driver(void);

static esp_err_t wifi_configure_driver(
    const system_config_t *cfg);

static esp_err_t wifi_start_driver(void);

static esp_err_t wifi_stop_driver(void);

static esp_err_t wifi_connect(
    const system_config_t *cfg);

static void wifi_reset_status(void);

static void wifi_prepare_connection(void);

static esp_err_t wifi_start_provisioning(void);

/* Updates the runtime WiFi connection status. */
static void wifi_set_status(
    wifi_manager_state_t state,
    bool got_ip,
    const esp_netif_ip_info_t *ip);

static esp_err_t wifi_register_handlers(void);

/* Protects concurrent access to the WiFi manager state and pending events. */
static void wifi_lock(void)
{
    xSemaphoreTake(
        s_wifi.mutex,
        portMAX_DELAY);
}

static void wifi_unlock(void)
{
    xSemaphoreGive(
        s_wifi.mutex);
}

/******************************************************************************
 * Event handlers
 ******************************************************************************/

/*
 * Triggered when the ESP32 station starts.
 * This initiates the connection attempt to the configured access point.
 */
static void wifi_handle_start(void)
{
    logger_info(
        TAG,
        "Station started");

    /* A station connection is intentionally skipped while awaiting setup. */
    if (config_get()->wifi_ssid[0] == '\0')
    {
        logger_info(TAG, "Provisioning access point ready");
        return;
    }

    esp_err_t err = esp_wifi_connect();

    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "Connect failed (%s)",
            esp_err_to_name(err));
    }
}

/*
 * Handles disconnect events and keeps reconnecting for the complete lifetime
 * of the controller. WIFI_FAIL_BIT reports a failed retry cycle but must never
 * stop subsequent connection attempts.
 */
static void wifi_handle_disconnect(
    wifi_event_sta_disconnected_t *event)
{
    logger_error(
        TAG,
        "Disconnected, reason=%d",
        event->reason);

    wifi_set_status(
        WIFI_MANAGER_DISCONNECTED,
        false,
        NULL);

    s_wifi.retry_count++;

    if (s_wifi.retry_count >= WIFI_MAXIMUM_RETRY)
    {
        /*
         * Report one failed retry cycle, then immediately begin another one.
         * The failure bit must not stop subsequent connection attempts.
         */
        xEventGroupSetBits(
            s_wifi.event_group,
            WIFI_FAIL_BIT);

        logger_warn(
            TAG,
            "WiFi still unavailable after %u attempts; retrying",
            (unsigned)WIFI_MAXIMUM_RETRY);

        s_wifi.retry_count =
            0;
    }

    /* Never abandon reconnection after a temporary access-point outage. */
    esp_err_t err =
        esp_wifi_connect();

    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "Reconnect failed (%s)",
            esp_err_to_name(err));
    }
}

/*
 * Updates the status as soon as the station receives an IP address from DHCP.
 */
static void wifi_handle_got_ip(
    ip_event_got_ip_t *event)
{
    s_wifi.retry_count = 0;

    wifi_set_status(
        WIFI_MANAGER_CONNECTED,
        true,
        &event->ip_info);

    xEventGroupSetBits(
        s_wifi.event_group,
        WIFI_CONNECTED_BIT);

    /* Clear the outage indication after a successful reconnection. */
    xEventGroupClearBits(
        s_wifi.event_group,
        WIFI_FAIL_BIT);

    logger_info(
        TAG,
        "IP acquired");

}

/* Callbacks ESP-IDF */
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_base;
    
    switch (event_id)
    {
        case WIFI_EVENT_STA_START:

            wifi_handle_start();

            break;

        case WIFI_EVENT_STA_DISCONNECTED:

            wifi_handle_disconnect(
                (wifi_event_sta_disconnected_t *)event_data);

        break;

    default:
        break;
    }    
}

static void ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_base;
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        wifi_handle_got_ip(
            (ip_event_got_ip_t *)event_data);
    }
}

/* Scan WiFi */

esp_err_t wifi_manager_scan(
    wifi_manager_ap_t *access_points,
    size_t capacity,
    size_t *count)
{
    if ((access_points == NULL) || (count == NULL) ||
        (capacity == 0) || (capacity > UINT16_MAX))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;

    if (!s_wifi.started)
    {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_ap_record_t *records = calloc(capacity, sizeof(*records));
    if (records == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    wifi_scan_config_t scan = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err == ESP_OK)
    {
        uint16_t found = (uint16_t)capacity;
        err = esp_wifi_scan_get_ap_records(&found, records);
        if (err == ESP_OK)
        {
            for (uint16_t i = 0; i < found; ++i)
            {
                strlcpy(access_points[i].ssid,
                        (const char *)records[i].ssid,
                        sizeof(access_points[i].ssid));
                access_points[i].rssi = records[i].rssi;
                access_points[i].secured =
                    (records[i].authmode != WIFI_AUTH_OPEN);
            }
            *count = found;
        }
    }

    free(records);
    return err;
}

/*
 * Performs an active WiFi scan to enumerate nearby access points.
 * This is useful during startup or troubleshooting to confirm the RF environment.
 */
static void wifi_scan_access_points(void)
{
    wifi_scan_config_t scan =
    {
        .show_hidden = true
    };

    const uint16_t max_scan_results = 20;
    uint16_t ap_count = 0;

    wifi_ap_record_t *ap_records = calloc(
        max_scan_results,
        sizeof(wifi_ap_record_t));

    if (ap_records == NULL)
    {
        logger_error(
            TAG,
            "Cannot allocate WiFi scan buffer");
        return;
    }

    esp_err_t err = esp_wifi_scan_start(
        &scan,
        true);

    if (err == ESP_OK)
    {
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count > max_scan_results)
        {
            ap_count = max_scan_results;
        }

        esp_wifi_scan_get_ap_records(
            &ap_count,
            ap_records);

        logger_info(
            TAG,
            "Found %u AP(s)",
            ap_count);

        for (uint16_t i = 0; i < ap_count; i++)
        {
            logger_info(
                TAG,
                "%2u : '%s' ch=%u RSSI=%d",
                i,
                (char *)ap_records[i].ssid,
                ap_records[i].primary,
                ap_records[i].rssi);
        }
    }
    else
    {
        logger_error(
            TAG,
            "Scan failed (%s)",
            esp_err_to_name(err));
    }

    free(ap_records);
}

/* Unregisters the WiFi and IP event handlers. */
static void wifi_unregister_handlers(void)
{
    if (s_wifi.wifi_handler != NULL)
    {
        esp_event_handler_instance_unregister(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            s_wifi.wifi_handler);

        s_wifi.wifi_handler = NULL;
    }

    if (s_wifi.ip_handler != NULL)
    {
        esp_event_handler_instance_unregister(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            s_wifi.ip_handler);

        s_wifi.ip_handler = NULL;
    }
}

static void wifi_cleanup(void)
{
    wifi_unregister_handlers();

    if (s_wifi.event_group != NULL)
    {
        vEventGroupDelete(
            s_wifi.event_group);

        s_wifi.event_group = NULL;
    }

    if (s_wifi.mutex != NULL)
    {
        vSemaphoreDelete(
            s_wifi.mutex);

        s_wifi.mutex = NULL;
    }

    s_wifi.initialized = false;
}

/* Creates the default station netif if it does not already exist. */
static esp_err_t wifi_create_netif(void)
{
    if (s_wifi.netif != NULL)
    {
        return ESP_OK;
    }

    s_wifi.netif = esp_netif_create_default_wifi_sta();

    if (s_wifi.netif == NULL)
    {
        logger_error(
            TAG,
            "Cannot create STA interface");

        return ESP_FAIL;
    }

    return ESP_OK;
}

/* Creates the default access-point interface used only for provisioning. */
static esp_err_t wifi_create_ap_netif(void)
{
    if (s_wifi.ap_netif != NULL)
    {
        return ESP_OK;
    }

    s_wifi.ap_netif = esp_netif_create_default_wifi_ap();

    return (s_wifi.ap_netif != NULL)
        ? ESP_OK
        : ESP_FAIL;
}

/* Loads or creates the WPA2 password used by the first-boot setup network. */
static esp_err_t wifi_setup_password(char password[WIFI_SETUP_PASSWORD_LENGTH + 1U])
{
    static const char alphabet[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    uint8_t random_bytes[WIFI_SETUP_PASSWORD_LENGTH];
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_setup", NVS_READWRITE, &nvs);

    if (err != ESP_OK)
    {
        return err;
    }

    size_t size = WIFI_SETUP_PASSWORD_LENGTH + 1U;
    err = nvs_get_str(nvs, "password", password, &size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        esp_fill_random(random_bytes, sizeof(random_bytes));

        for (size_t i = 0; i < WIFI_SETUP_PASSWORD_LENGTH; ++i)
        {
            password[i] = alphabet[random_bytes[i] % (sizeof(alphabet) - 1U)];
        }

        password[WIFI_SETUP_PASSWORD_LENGTH] = '\0';
        memset(random_bytes, 0, sizeof(random_bytes));

        err = nvs_set_str(nvs, "password", password);

        if (err == ESP_OK)
        {
            err = nvs_commit(nvs);
        }
    }
    else if ((err == ESP_OK) &&
             (size != WIFI_SETUP_PASSWORD_LENGTH + 1U))
    {
        err = ESP_ERR_INVALID_SIZE;
    }

    nvs_close(nvs);
    return err;
}

/* Configures an isolated WPA2 setup network when no station SSID exists. */
static esp_err_t wifi_start_provisioning(void)
{
    esp_err_t err = wifi_create_netif();

    if (err != ESP_OK)
    {
        return err;
    }

    err = wifi_create_ap_netif();

    if (err != ESP_OK)
    {
        return err;
    }

    err = wifi_init_driver();

    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);

    if (err != ESP_OK)
    {
        return err;
    }

    char password[WIFI_SETUP_PASSWORD_LENGTH + 1U];
    err = wifi_setup_password(password);

    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    wifi_config_t ap_config = {0};
    (void)snprintf(
        (char *)ap_config.ap.ssid,
        sizeof(ap_config.ap.ssid),
        "TWC-Setup-%02X%02X%02X",
        mac[3], mac[4], mac[5]);
    strlcpy(
        (char *)ap_config.ap.password,
        password,
        sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1U;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 2U;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    if (err == ESP_OK)
    {
        err = esp_wifi_start();
    }

    if (err == ESP_OK)
    {
        s_wifi.provisioning = true;
        printf("SETUP WIFI SSID: %s\n", ap_config.ap.ssid);
        printf("SETUP WIFI PASSWORD (serial only): %s\n", password);
        printf("SETUP URL: http://192.168.4.1/\n");
    }

    memset(password, 0, sizeof(password));
    return err;
}

/* Initializes the ESP-IDF WiFi driver and configures the station mode. */
static esp_err_t wifi_init_driver(void)
{
    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    esp_err_t err =
        esp_wifi_init(&cfg);

    if (err == ESP_ERR_WIFI_INIT_STATE)
    {
        err = ESP_OK;
    }

    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "esp_wifi_init failed (%s)",
            esp_err_to_name(err));

        return err;
    }

    err = esp_wifi_set_storage(
        WIFI_STORAGE_RAM);

    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_set_mode(
        WIFI_MODE_STA);

    return err;
}

/* Applies the configured SSID and password to the station interface. */
static esp_err_t wifi_configure_driver(
    const system_config_t *cfg)
{
    wifi_config_t wifi_cfg = {0};

    strlcpy(
        (char *)wifi_cfg.sta.ssid,
        cfg->wifi_ssid,
        sizeof(wifi_cfg.sta.ssid));

    strlcpy(
        (char *)wifi_cfg.sta.password,
        cfg->wifi_password,
        sizeof(wifi_cfg.sta.password));

    /* Permit an open station only when the saved password is empty. */
    wifi_cfg.sta.threshold.authmode =
        (cfg->wifi_password[0] == '\0')
            ? WIFI_AUTH_OPEN
            : WIFI_AUTH_WPA2_PSK;

    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    return esp_wifi_set_config(
        WIFI_IF_STA,
        &wifi_cfg);
}

/* Creates, configures and starts the WiFi station. */
static esp_err_t wifi_connect(
    const system_config_t *cfg)
{
    esp_err_t err;

    err = wifi_create_netif();

    if (err != ESP_OK)
    {
        return err;
    }

    err = wifi_init_driver();

    if (err != ESP_OK)
    {
        return err;
    }

    err = wifi_configure_driver(
        cfg);

    if (err != ESP_OK)
    {
        return err;
    }

    return wifi_start_driver();
}

/* Starts the WiFi driver and triggers the scan before connecting. */
static esp_err_t wifi_start_driver(void)
{
    esp_err_t err;

    err = esp_wifi_start();

    if (err == ESP_ERR_WIFI_CONN)
    {
        err = ESP_OK;
    }

    if (err == ESP_OK)
    {
        wifi_scan_access_points();
    }

    return err;
}

static esp_err_t wifi_stop_driver(void)
{
    logger_info(
        TAG,
        "Stopping WiFi");

    return esp_wifi_stop();
}

/* Resets runtime status when the WiFi stack is stopped or a fresh connection attempt is required. */
static void wifi_reset_status(void)
{
    s_wifi.started = false;

    s_wifi.retry_count = 0;

    wifi_set_status(
        WIFI_MANAGER_DISCONNECTED,
        false,
        NULL);
}

/* Updates the runtime WiFi connection status. */
static void wifi_set_status(
    wifi_manager_state_t state,
    bool got_ip,
    const esp_netif_ip_info_t *ip)
{
    s_wifi.status.state = state;

    s_wifi.status.got_ip = got_ip;

    if (ip != NULL)
    {
        s_wifi.status.ip = *ip;
    }
    else
    {
        memset(
            &s_wifi.status.ip,
            0,
            sizeof(s_wifi.status.ip));
    }
}

static void wifi_prepare_connection(void)
{
    s_wifi.retry_count = 0;

    /* Start a clean connection cycle without stale status bits. */
    xEventGroupClearBits(
        s_wifi.event_group,
        WIFI_CONNECTED_BIT |
        WIFI_FAIL_BIT);

    wifi_set_status(
        WIFI_MANAGER_CONNECTING,
        false,
        NULL);
}

/* Registers the WiFi and IP event handlers. */
static esp_err_t wifi_register_handlers(void)
{
    esp_err_t err;

    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL,
        &s_wifi.wifi_handler);

    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "Cannot register WIFI handler (%s)",
            esp_err_to_name(err));

        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        ip_event_handler,
        NULL,
        &s_wifi.ip_handler);

    if (err != ESP_OK)
    {
        logger_error(
            TAG,
            "Cannot register IP handler (%s)",
            esp_err_to_name(err));

        esp_event_handler_instance_unregister(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            s_wifi.wifi_handler);

        s_wifi.wifi_handler = NULL;

        return err;
    }

    return ESP_OK;
}

/* Public API */

/*
 * Initializes the manager, creates its mutex/event group, and registers the
 * ESP-IDF WiFi and IP event handlers.
 */
esp_err_t wifi_manager_init(void)
{
    esp_err_t err;

    err = wifi_init_driver();

    if (err != ESP_OK)
    {
        wifi_cleanup();

        return err;
    }

    wifi_set_status(
        WIFI_MANAGER_DISCONNECTED,
        false,
        NULL);

    s_wifi.mutex = xSemaphoreCreateMutex();

    if (s_wifi.mutex == NULL)
    {
        logger_error(
        TAG,
        "Cannot create mutex");

        return ESP_ERR_NO_MEM;
    }

    s_wifi.event_group = xEventGroupCreate();

    if (s_wifi.event_group == NULL)
    {
        logger_error(
            TAG,
            "Cannot create EventGroup");

        wifi_cleanup();

        return ESP_ERR_NO_MEM;
    }

    err = wifi_register_handlers();

    if (err != ESP_OK)
    {
        wifi_cleanup();

        return err;
    }    

    s_wifi.initialized = true;

    logger_info(
        TAG,
        "Connection started");

    return ESP_OK;
}

/*
 * Starts the WiFi station using the persisted SSID/password from the system
 * configuration and begins the connection process.
 */
esp_err_t wifi_manager_start(void)
{
    esp_err_t err;

    const system_config_t *cfg = config_get();

    if (!s_wifi.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi.started)
    {
        return ESP_OK;
    }

    wifi_lock();

    if (cfg->wifi_ssid[0] == '\0')
    {
        err = wifi_start_provisioning();
    }
    else
    {
        err = wifi_connect(cfg);
    }

    wifi_unlock();

    if (err != ESP_OK)
    {
        return err;
    }

    s_wifi.started = true;

    if (cfg->wifi_ssid[0] != '\0')
    {
        logger_info(
            TAG,
            "Connecting to %s",
            cfg->wifi_ssid);
    }

    logger_info(
        TAG,
        "Connection started");

    return ESP_OK;
}

/* Stops the WiFi station and resets the connection state. */
esp_err_t wifi_manager_stop(void)
{
    if (!s_wifi.started)
    {
        return ESP_OK;
    }

    esp_err_t err = wifi_stop_driver();

    if (err != ESP_OK)
    {
        return err;
    }

    wifi_reset_status();

    return ESP_OK;
}

/* Releases the WiFi resources and unregisters all ESP-IDF callbacks. */
esp_err_t wifi_manager_deinit(void)
{
    if (!s_wifi.initialized)
    {
        return ESP_OK;
    }

    wifi_manager_stop();

    if (s_wifi.netif != NULL)
    {
        esp_netif_destroy(
            s_wifi.netif);

        s_wifi.netif = NULL;
    }

    if (s_wifi.ap_netif != NULL)
    {
        esp_netif_destroy(s_wifi.ap_netif);
        s_wifi.ap_netif = NULL;
    }

    wifi_cleanup();

    memset(
        &s_wifi,
        0,
        sizeof(s_wifi));

    logger_info(
        TAG,
        "WiFi manager deinitialized");

    return ESP_OK;
}

/* Returns true when the station has successfully received an IP address. */
bool wifi_manager_is_connected(void)
{
    return s_wifi.status.got_ip;
}

/* Returns the current high-level connection state of the WiFi station. */
wifi_manager_state_t wifi_manager_get_state(void)
{
    return s_wifi.status.state;
}

const wifi_manager_status_t *wifi_manager_get_status(void)
{
    return &s_wifi.status;
}

const esp_netif_ip_info_t *wifi_manager_get_ip(void)
{
    if (!s_wifi.status.got_ip)
    {
        return NULL;
    }

    return &s_wifi.status.ip;
}

esp_err_t wifi_manager_reconnect(void)
{
    if (!s_wifi.started)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Reapply the newly persisted credentials instead of reconnecting with
     * the stale driver configuration. This also leaves provisioning mode. */
    esp_err_t err = wifi_stop_driver();

    if (err != ESP_OK)
    {
        return err;
    }

    s_wifi.provisioning = false;
    wifi_prepare_connection();

    if (config_get()->wifi_ssid[0] == '\0')
    {
        return wifi_start_provisioning();
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err == ESP_OK)
    {
        err = wifi_configure_driver(config_get());
    }

    if (err == ESP_OK)
    {
        err = wifi_start_driver();
    }

    return err;
}
