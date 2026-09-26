#ifndef AIDE_HASH_H
#define AIDE_HASH_H

/* Small string-keyed maps shared by core/ modules. AideStrIntMap is a
 * linear-scan association list -- fine for the small, per-file maps it's
 * used for today (StructLayout's knownStructSizes, a handful of entries).
 * If a module needs real hashing at scale (e.g. AutodocIndex's few
 * thousand entries) it gets its own bucketed table rather than forcing
 * every caller through one generic hash implementation. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideStrIntEntry {
    char *key;
    int value;
} AideStrIntEntry;

typedef struct AideStrIntMap {
    AideStrIntEntry *items;
    size_t count;
    size_t cap;
} AideStrIntMap;

void aide_strintmap_init(AideStrIntMap *m);
void aide_strintmap_free(AideStrIntMap *m);
void aide_strintmap_clear(AideStrIntMap *m);
/* Inserts, or overwrites the value if `key` is already present. */
void aide_strintmap_set(AideStrIntMap *m, const char *key, int value);
/* Returns the stored value, or `def` if `key` isn't present. */
int aide_strintmap_get(const AideStrIntMap *m, const char *key, int def);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_HASH_H */
