#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_netif.h"

/*
 * Minimal JSON writer used by the web API to build compact responses
 * without introducing a heavyweight JSON library.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define JSON_MAX_DEPTH 8

typedef struct
{
    char *buffer;

    size_t size;

    size_t pos;

    uint8_t depth;

    bool first[JSON_MAX_DEPTH];

    bool overflow;

} json_writer_t;

/* Initialisation */

void json_init(
    json_writer_t *json,
    char *buffer,
    size_t size);

bool json_ok(
    const json_writer_t *json);

/* Structure JSON */

void json_begin_object(
    json_writer_t *json);

void json_end_object(
    json_writer_t *json);

void json_end_array(
    json_writer_t *json);

void json_begin_named_object(
    json_writer_t *json,
    const char *name);

void json_begin_named_array(
    json_writer_t *json,
    const char *name);

/* Valeurs */

void json_write_string(
    json_writer_t *json,
    const char *name,
    const char *value);

void json_write_int(
    json_writer_t *json,
    const char *name,
    int value);

void json_write_double(
    json_writer_t *json,
    const char *name,
    double value);

void json_write_uint(
    json_writer_t *json,
    const char *name,
    unsigned value);

void json_write_uint64(
    json_writer_t *json,
    const char *name,
    uint64_t value);

void json_write_ip(
    json_writer_t *json,
    const char *name,
    const esp_ip4_addr_t *ip);    

void json_write_mac(
    json_writer_t *json,
    const char *name,
    const uint8_t mac[6]);

void json_write_bool(
    json_writer_t *json,
    const char *name,
    bool value);

/* Tableau */

void json_array_string(
    json_writer_t *json,
    const char *value);

void json_array_begin_object(
    json_writer_t *json);

void json_array_end_object(
    json_writer_t *json);

/* Utilitaires */

size_t json_escape_string(
    char *dst,
    size_t dst_size,
    const char *src);

#ifdef __cplusplus
}
#endif

#endif
