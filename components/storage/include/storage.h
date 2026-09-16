
/*
 * Persistent storage abstraction built on top of ESP-IDF NVS.
 * It exposes basic read/write/erase helpers for configuration blobs.
 */

 #ifndef STORAGE_H_
#define STORAGE_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>

/* Initializes the persistent storage subsystem. */
bool storage_init(void);

/* Reads a binary object from persistent storage. */
bool storage_read(
    const char *key,
    void *buffer,
    size_t size);

    /* Writes a binary object to persistent storage. */
bool storage_write(
    const char *key,
    const void *buffer,
    size_t size);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H_ */
