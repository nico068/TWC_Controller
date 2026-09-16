/******************************************************************************
 * api.c
 * Shared helpers for formatting and returning standardized HTTP JSON responses.
 ******************************************************************************/

#include "api.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "nvs.h"

#define API_AUTH_TOKEN_BYTES 16U
#define API_AUTH_TOKEN_HEX_LENGTH (API_AUTH_TOKEN_BYTES * 2U)

static char s_admin_token[API_AUTH_TOKEN_HEX_LENGTH + 1U];

/* Loads the administrator secret or creates it on the first boot. */
esp_err_t api_auth_init(void)
{
    uint8_t secret[API_AUTH_TOKEN_BYTES];
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("api_auth", NVS_READWRITE, &nvs);

    if (err != ESP_OK)
    {
        return err;
    }

    size_t size = sizeof(secret);
    err = nvs_get_blob(nvs, "secret", secret, &size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        esp_fill_random(secret, sizeof(secret));
        err = nvs_set_blob(nvs, "secret", secret, sizeof(secret));

        if (err == ESP_OK)
        {
            err = nvs_commit(nvs);
        }
    }
    else if ((err == ESP_OK) && (size != sizeof(secret)))
    {
        err = ESP_ERR_INVALID_SIZE;
    }

    nvs_close(nvs);

    if (err != ESP_OK)
    {
        memset(secret, 0, sizeof(secret));
        return err;
    }

    for (size_t i = 0; i < sizeof(secret); ++i)
    {
        (void)snprintf(
            &s_admin_token[i * 2U],
            3U,
            "%02x",
            secret[i]);
    }

    memset(secret, 0, sizeof(secret));

    /* The token is deliberately available only through the local serial console. */
    printf("ADMIN TOKEN (serial only): %s\n", s_admin_token);

    return ESP_OK;
}

/* Compares two fixed-size tokens without leaking the matching prefix length. */
static bool api_auth_token_matches(const char *candidate)
{
    unsigned int difference = 0U;

    for (size_t i = 0; i < API_AUTH_TOKEN_HEX_LENGTH; ++i)
    {
        difference |= (unsigned int)
            ((uint8_t)candidate[i] ^ (uint8_t)s_admin_token[i]);
    }

    return (difference == 0U) &&
        (candidate[API_AUTH_TOKEN_HEX_LENGTH] == '\0');
}

/* Requires a Bearer token for every protected API request. */
bool api_require_auth(httpd_req_t *req)
{
    static const char prefix[] = "Bearer ";
    char authorization[sizeof(prefix) + API_AUTH_TOKEN_HEX_LENGTH] = {0};

    if ((req != NULL) &&
        (s_admin_token[0] != '\0') &&
        (httpd_req_get_hdr_value_str(
            req,
            "Authorization",
            authorization,
            sizeof(authorization)) == ESP_OK) &&
        (strncmp(authorization, prefix, sizeof(prefix) - 1U) == 0) &&
        api_auth_token_matches(authorization + sizeof(prefix) - 1U))
    {
        return true;
    }

    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
    (void)httpd_resp_send_err(
        req,
        HTTPD_401_UNAUTHORIZED,
        "Administrator token required");

    return false;
}

/* Sends a JSON response. */
esp_err_t api_send_json(
    httpd_req_t *req,
    const char *json)
{
    httpd_resp_set_type(
        req,
        "application/json");

    /* Sensitive API responses must never be stored by browsers or proxies. */
    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store");

    return httpd_resp_send(
        req,
        json,
        HTTPD_RESP_USE_STRLEN);
}

/* Sends a generic success response. */
esp_err_t api_send_ok(
    httpd_req_t *req)
{
    return api_send_json(
        req,
        "{\"success\":true}");
}

/* Sends an HTTP error response. */
esp_err_t api_send_error(
    httpd_req_t *req,
    httpd_err_code_t code,
    const char *message)
{
    return httpd_resp_send_err(
        req,
        code,
        message);
}
