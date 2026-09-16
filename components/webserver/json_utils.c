/******************************************************************************
 * json_utils.c * Small JSON writer used to create compact API responses without depending on
 * a heavy external serialization library. ******************************************************************************/

#include "json_utils.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>

/******************************************************************************
 * Fonction interne
 ******************************************************************************/

static void json_separator(
    json_writer_t *json)
{
    if (json->depth == 0)
    {
        return;
    }

    if (json->first[json->depth - 1])
    {
        json->first[json->depth - 1] = false;
    }
    else
    {
        int len = snprintf(
            json->buffer + json->pos,
            json->size - json->pos,
            ",");

        if ((len < 0) ||
            ((size_t)len >= (json->size - json->pos)))
        {
            json->overflow = true;
            return;
        }

        json->pos += (size_t)len;
    }
}

/******************************************************************************
 * Fonction interne
 ******************************************************************************/

static void json_append(
    json_writer_t *json,
    const char *fmt,
    ...)
{
    if (json->overflow)
    {
        return;
    }

    va_list args;

    va_start(
        args,
        fmt);

    int len = vsnprintf(
        json->buffer + json->pos,
        json->size - json->pos,
        fmt,
        args);

    va_end(args);

    if ((len < 0) ||
        ((size_t)len >= (json->size - json->pos)))
    {
        json->overflow = true;
        return;
    }

    json->pos += (size_t)len;
}

/******************************************************************************
 * Initialisation
 ******************************************************************************/

void json_init(
    json_writer_t *json,
    char *buffer,
    size_t size)
{
    memset(
        json,
        0,
        sizeof(*json));

    json->buffer = buffer;

    json->size = size;

    if (size > 0)
    {
        buffer[0] = '\0';
    }
}

/******************************************************************************
 * Etat
 ******************************************************************************/

bool json_ok(
    const json_writer_t *json)
{
    return !json->overflow;
}

/******************************************************************************
 * Objet JSON
 ******************************************************************************/

void json_begin_object(
    json_writer_t *json)
{
    json_append(
        json,
        "{");

    if (json->depth < JSON_MAX_DEPTH)
    {
        json->first[json->depth] = true;
        json->depth++;
    }
    else
    {
        json->overflow = true;
    }
}

void json_end_object(
    json_writer_t *json)
{
    if (json->depth == 0)
    {
        json->overflow = true;
        return;
    }

    json->depth--;

    json_append(
        json,
        "}");
}

/******************************************************************************
 * Tableau JSON
 ******************************************************************************/

void json_end_array(
    json_writer_t *json)
{
    if (json->depth == 0)
    {
        json->overflow = true;
        return;
    }

    json->depth--;

    json_append(
        json,
        "]");
}

/******************************************************************************
 * Objet/Tableau nommé
 ******************************************************************************/

void json_begin_named_object(
    json_writer_t *json,
    const char *name)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":{",
        name);

    if (json->depth < JSON_MAX_DEPTH)
    {
        json->first[json->depth] = true;
        json->depth++;
    }
    else
    {
        json->overflow = true;
    }
}

void json_begin_named_array(
    json_writer_t *json,
    const char *name)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":[",
        name);

    if (json->depth < JSON_MAX_DEPTH)
    {
        json->first[json->depth] = true;
        json->depth++;
    }
    else
    {
        json->overflow = true;
    }
}

/******************************************************************************
 * Echappement d'une chaîne JSON
 ******************************************************************************/

size_t json_escape_string(
    char *dst,
    size_t dst_size,
    const char *src)
{
    size_t pos = 0;

    if ((dst == NULL) ||
        (src == NULL) ||
        (dst_size == 0))
    {
        return 0;
    }

    while (*src)
    {
        switch (*src)
        {
            case '\"':

                if (pos + 2 >= dst_size)
                {
                    goto end;
                }

                dst[pos++] = '\\';
                dst[pos++] = '\"';
                break;

            case '\\':

                if (pos + 2 >= dst_size)
                {
                    goto end;
                }

                dst[pos++] = '\\';
                dst[pos++] = '\\';
                break;

            case '\n':

                if (pos + 2 >= dst_size)
                {
                    goto end;
                }

                dst[pos++] = '\\';
                dst[pos++] = 'n';
                break;

            case '\r':

                if (pos + 2 >= dst_size)
                {
                    goto end;
                }

                dst[pos++] = '\\';
                dst[pos++] = 'r';
                break;

            case '\t':

                if (pos + 2 >= dst_size)
                {
                    goto end;
                }

                dst[pos++] = '\\';
                dst[pos++] = 't';
                break;

            default:

                if (pos + 1 >= dst_size)
                {
                    goto end;
                }

                dst[pos++] = *src;
                break;
        }

        src++;
    }

end:

    dst[pos] = '\0';

    return pos;
}

/******************************************************************************
 * Ecriture d'une chaîne
 ******************************************************************************/

void json_write_string(
    json_writer_t *json,
    const char *name,
    const char *value)
{
    char escaped[256];

    json_escape_string(
        escaped,
        sizeof(escaped),
        value ? value : "");

    json_separator(json);

    json_append(
        json,
        "\"%s\":\"%s\"",
        name,
        escaped);
}

void json_write_int(
    json_writer_t *json,
    const char *name,
    int value)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":%d",
        name,
        value);
}

void json_write_double(
    json_writer_t *json,
    const char *name,
    double value)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":%.2f",
        name,
        value);
}

void json_write_uint(
    json_writer_t *json,
    const char *name,
    unsigned value)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":%u",
        name,
        value);
}

void json_write_uint64(
    json_writer_t *json,
    const char *name,
    uint64_t value)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":%" PRIu64,
        name,
        value);
}

void json_write_ip(
    json_writer_t *json,
    const char *name,
    const esp_ip4_addr_t *ip)
{
    char address[16];

    snprintf(
        address,
        sizeof(address),
        IPSTR,
        IP2STR(ip));

    json_write_string(
        json,
        name,
        address);
}

void json_write_mac(
    json_writer_t *json,
    const char *name,
    const uint8_t mac[6])
{
    char address[18];

    if (mac == NULL)
    {
        json_write_string(
            json,
            name,
            "");

        return;
    }

    snprintf(
        address,
        sizeof(address),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);

    json_write_string(
        json,
        name,
        address);
}

void json_write_bool(
    json_writer_t *json,
    const char *name,
    bool value)
{
    json_separator(json);

    json_append(
        json,
        "\"%s\":%s",
        name,
        value ? "true" : "false");
}

/******************************************************************************
 * Valeurs de tableau
 ******************************************************************************/

void json_array_string(
    json_writer_t *json,
    const char *value)
{
    char escaped[256];

    json_escape_string(
        escaped,
        sizeof(escaped),
        value ? value : "");

    json_separator(json);

    json_append(
        json,
        "\"%s\"",
        escaped);
}

/******************************************************************************
 * Objet dans un tableau
 ******************************************************************************/

void json_array_begin_object(
    json_writer_t *json)
{
    json_separator(json);

    json_append(
        json,
        "{");

    if (json->depth < JSON_MAX_DEPTH)
    {
        json->first[json->depth] = true;
        json->depth++;
    }
    else
    {
        json->overflow = true;
    }
}

void json_array_end_object(
    json_writer_t *json)
{
    if (json->depth == 0)
    {
        json->overflow = true;
        return;
    }

    json->depth--;

    json_append(
        json,
        "}");
}
