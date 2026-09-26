#include "aide/aide_includegraph.h"
#include "aide/aide_fs.h"

#include <stdlib.h>
#include <string.h>

void aide_includegraph_init(AideIncludeGraph *g)
{
    g->entries = NULL;
    g->count = 0;
    g->cap = 0;
}

void aide_includegraph_free(AideIncludeGraph *g)
{
    size_t i;
    for (i = 0; i < g->count; ++i) {
        free(g->entries[i].file);
        aide_strlist_free(&g->entries[i].direct);
    }
    free(g->entries);
    g->entries = NULL;
    g->count = 0;
    g->cap = 0;
}

static AideIncludeGraphEntry *find_entry(AideIncludeGraph *g, const char *file)
{
    size_t i;
    for (i = 0; i < g->count; ++i) {
        if (strcmp(g->entries[i].file, file) == 0)
            return &g->entries[i];
    }
    return NULL;
}

static const AideIncludeGraphEntry *find_entry_const(const AideIncludeGraph *g, const char *file)
{
    size_t i;
    for (i = 0; i < g->count; ++i) {
        if (strcmp(g->entries[i].file, file) == 0)
            return &g->entries[i];
    }
    return NULL;
}

void aide_includegraph_set_direct(AideIncludeGraph *g, const char *file, const AideStrList *resolvedAbsoluteIncludes)
{
    AideIncludeGraphEntry *e = find_entry(g, file);
    size_t i;

    if (!e) {
        if (g->count >= g->cap) {
            size_t newCap = g->cap > 0 ? g->cap * 2 : 16;
            g->entries = (AideIncludeGraphEntry *)realloc(g->entries, newCap * sizeof(AideIncludeGraphEntry));
            g->cap = newCap;
        }
        e = &g->entries[g->count++];
        e->file = aide_strdup(file);
        aide_strlist_init(&e->direct);
    }

    aide_strlist_clear(&e->direct);
    for (i = 0; i < resolvedAbsoluteIncludes->count; ++i)
        aide_strlist_push(&e->direct, resolvedAbsoluteIncludes->items[i]);
}

void aide_includegraph_remove_file(AideIncludeGraph *g, const char *file)
{
    size_t i;
    for (i = 0; i < g->count; ++i) {
        if (strcmp(g->entries[i].file, file) == 0) {
            free(g->entries[i].file);
            aide_strlist_free(&g->entries[i].direct);
            memmove(&g->entries[i], &g->entries[i + 1], (g->count - i - 1) * sizeof(AideIncludeGraphEntry));
            g->count--;
            return;
        }
    }
}

void aide_includegraph_direct_includes(const AideIncludeGraph *g, const char *file, AideStrList *out)
{
    const AideIncludeGraphEntry *e = find_entry_const(g, file);
    size_t i;

    aide_strlist_clear(out);
    if (!e)
        return;
    for (i = 0; i < e->direct.count; ++i)
        aide_strlist_push(out, e->direct.items[i]);
}

void aide_includegraph_transitive_includes(const AideIncludeGraph *g, const char *file, AideStrList *out)
{
    AideStrList stack;
    AideStrList visited;
    const AideIncludeGraphEntry *e;
    size_t i;

    aide_strlist_clear(out);
    aide_strlist_init(&stack);
    aide_strlist_init(&visited);

    e = find_entry_const(g, file);
    if (e) {
        for (i = 0; i < e->direct.count; ++i)
            aide_strlist_push(&stack, e->direct.items[i]);
    }

    while (stack.count > 0) {
        char *current = stack.items[stack.count - 1];
        int alreadyVisited = 0;

        for (i = 0; i < visited.count; ++i) {
            if (strcmp(visited.items[i], current) == 0) { alreadyVisited = 1; break; }
        }
        if (alreadyVisited) {
            free(current);
            stack.count--;
            continue;
        }
        aide_strlist_push(&visited, current);
        aide_strlist_push(out, current);

        e = find_entry_const(g, current);
        free(current);
        stack.count--;

        if (e) {
            for (i = 0; i < e->direct.count; ++i)
                aide_strlist_push(&stack, e->direct.items[i]);
        }
    }

    aide_strlist_free(&stack);
    aide_strlist_free(&visited);
}

char *aide_includegraph_resolve(const char *directive, int quoted, const char *includingFileDir,
                                 const AideStrList *searchPaths)
{
    size_t i;

    if (quoted) {
        char *candidate = aide_fs_join(includingFileDir, directive);
        if (aide_fs_exists(candidate)) {
            char *canon = aide_fs_canonical(candidate);
            free(candidate);
            return canon;
        }
        free(candidate);
    }

    for (i = 0; i < searchPaths->count; ++i) {
        char *candidate = aide_fs_join(searchPaths->items[i], directive);
        if (aide_fs_exists(candidate)) {
            char *canon = aide_fs_canonical(candidate);
            free(candidate);
            return canon;
        }
        free(candidate);
    }

    return NULL;
}
