/* NVS-backed storage manager used to persist configuration data across resets. */

#include "storage.h"
#include "logger.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STORAGE_NAMESPACE "config"

static nvs_handle_t s_nvs = 0;

static bool s_initialized = false;

static bool storage_is_ready(void)
{
    return s_initialized;
}

/* Returns whether a storage key is valid. */
static bool storage_is_valid_key(
    const char *key)
{
    return
        storage_is_ready() &&
        (key != NULL);
}

/* Returns whether a buffer operation is valid. */
static bool storage_is_valid_buffer(
    const char *key,
    const void *buffer,
    size_t size)
{
    return
        storage_is_valid_key(key) &&
        (buffer != NULL) &&
        (size > 0);
}

/* Commits pending changes to persistent storage. */
static bool storage_commit(void)
{
    esp_err_t err =
        nvs_commit(s_nvs);

    if (err != ESP_OK)
    {
        /* Report the NVS commit error before returning failure. */
        logger_error(
            "STORAGE",
            "nvs_commit failed (%s)",
            esp_err_to_name(err));

        return false;
    }

    return true;
}

/* Opens the persistent storage namespace. */
static bool storage_open(void)
{
    esp_err_t err =
        nvs_open(
            STORAGE_NAMESPACE,
            NVS_READWRITE,
            &s_nvs);

    if (err != ESP_OK)
    {
        logger_write(
            LOGGER_ERROR,
            "STORAGE",
            "NVS open failed: %s",
            esp_err_to_name(err));

        return false;
    }

    return true;
}

/* Initializes the NVS flash partition. */
static bool storage_init_flash(void)
{
    esp_err_t err =
        nvs_flash_init();

    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        logger_warn(
            "STORAGE",
            "Erasing NVS partition");

        err =
            nvs_flash_erase();

        if (err != ESP_OK)
        {
            logger_error(
                "STORAGE",
                "NVS erase failed: %s",
                esp_err_to_name(err));

            return false;
        }

        err =
            nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        logger_error(
            "STORAGE",
            "NVS initialization failed: %s",
            esp_err_to_name(err));

        return false;
    }

    return true;
}

bool storage_init(void)
{    
    if (s_initialized)
    {
        return true;
    }

    if (!storage_init_flash())
    {
        return false;
    }

    if (!storage_open())
    {
        return false;
    }

    s_initialized = true;

    logger_write(
        LOGGER_INFO,
        "STORAGE",
        "NVS initialized");

    return true;
}

bool storage_read(
    const char *key,
    void *buffer,
    size_t size)
{
    esp_err_t err;
    size_t stored_size = size;

    if (!storage_is_valid_buffer(
        key,
        buffer,
        size))
    {
        return false;
    }

    err = nvs_get_blob(
        s_nvs,
        key,
        buffer,
        &stored_size);

    if (err != ESP_OK)
    {
        return false;
    }

    if (stored_size != size)
    {
        return false;
    }

    return true;
}

bool storage_write(
    const char *key,
    const void *buffer,
    size_t size)
{
    esp_err_t err;

    if (!storage_is_valid_buffer(
        key,
        buffer,
        size))
    {
        return false;
    }

    err = nvs_set_blob(
    s_nvs,
    key,
    buffer,
    size);

    if (err != ESP_OK)
    {
        /* Report the NVS write error before returning failure. */
        logger_error(
            "STORAGE",
            "nvs_set_blob failed for key '%s' (%s)",
            key,
            esp_err_to_name(err));

        return false;
    }

    return storage_commit();
}
