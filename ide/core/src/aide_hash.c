#include "aide/aide_hash.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

void aide_strintmap_init(AideStrIntMap *m)
{
    m->items = NULL;
    m->count = 0;
    m->cap = 0;
}

void aide_strintmap_clear(AideStrIntMap *m)
{
    size_t i;
    for (i = 0; i < m->count; ++i)
        free(m->items[i].key);
    m->count = 0;
}

void aide_strintmap_free(AideStrIntMap *m)
{
    aide_strintmap_clear(m);
    free(m->items);
    m->items = NULL;
    m->cap = 0;
}

static void reserve(AideStrIntMap *m, size_t minCap)
{
    size_t newCap;
    AideStrIntEntry *newItems;
    if (m->cap >= minCap)
        return;
    newCap = m->cap > 0 ? m->cap * 2 : 8;
    while (newCap < minCap)
        newCap *= 2;
    newItems = (AideStrIntEntry *)malloc(newCap * sizeof(AideStrIntEntry));
    memcpy(newItems, m->items, m->count * sizeof(AideStrIntEntry));
    free(m->items);
    m->items = newItems;
    m->cap = newCap;
}

void aide_strintmap_set(AideStrIntMap *m, const char *key, int value)
{
    size_t i;
    for (i = 0; i < m->count; ++i) {
        if (strcmp(m->items[i].key, key) == 0) {
            m->items[i].value = value;
            return;
        }
    }
    reserve(m, m->count + 1);
    m->items[m->count].key = aide_strdup(key);
    m->items[m->count].value = value;
    m->count++;
}

int aide_strintmap_get(const AideStrIntMap *m, const char *key, int def)
{
    size_t i;
    for (i = 0; i < m->count; ++i) {
        if (strcmp(m->items[i].key, key) == 0)
            return m->items[i].value;
    }
    return def;
}
