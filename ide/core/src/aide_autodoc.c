#include "aide/aide_autodoc.h"
#include "aide/aide_fs.h"
#include "aide/aide_regex.h"

#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- AideAutodocEntry ---- */

void aide_autodocentry_free(AideAutodocEntry *e)
{
    free(e->function);
    free(e->library);
    free(e->docFile);
    e->function = e->library = e->docFile = NULL;
}

void aide_autodocentry_copy(AideAutodocEntry *dst, const AideAutodocEntry *src)
{
    dst->function = aide_strdup(src->function);
    dst->library = aide_strdup(src->library);
    dst->docFile = aide_strdup(src->docFile);
    dst->offset = src->offset;
    dst->length = src->length;
}

/* ---- growable entry array (used both as the index's storage and as a
 * scratch scan-result / cache-entries list) ---- */

typedef struct EntryArray {
    AideAutodocEntry *items;
    size_t count;
    size_t cap;
} EntryArray;

static void entryarray_init(EntryArray *a) { a->items = NULL; a->count = 0; a->cap = 0; }

static void entryarray_free(EntryArray *a)
{
    size_t i;
    for (i = 0; i < a->count; ++i)
        aide_autodocentry_free(&a->items[i]);
    free(a->items);
    a->items = NULL;
    a->count = 0;
    a->cap = 0;
}

static AideAutodocEntry *entryarray_push_new(EntryArray *a)
{
    if (a->count >= a->cap) {
        size_t newCap = a->cap > 0 ? a->cap * 2 : 64;
        a->items = (AideAutodocEntry *)realloc(a->items, newCap * sizeof(AideAutodocEntry));
        a->cap = newCap;
    }
    memset(&a->items[a->count], 0, sizeof(AideAutodocEntry));
    return &a->items[a->count++];
}

static void entryarray_push_copy(EntryArray *a, const AideAutodocEntry *src)
{
    AideAutodocEntry *dst = entryarray_push_new(a);
    aide_autodocentry_copy(dst, src);
}

/* ---- scanning ---- */

static AideRegex *s_headerRe = NULL;

static void ensure_compiled(void)
{
    if (!s_headerRe)
        s_headerRe = aide_regex_compile("^[[:space:]]*([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)", 0);
}

static void scan_file(const char *path, EntryArray *out)
{
    char *text;
    size_t size;
    long chunkStart, contentStart, chunkEnd;

    ensure_compiled();
    if (!aide_fs_read_file(path, &text, &size))
        return;

    chunkStart = -1;
    {
        size_t i;
        for (i = 0; i < size; ++i) {
            if ((unsigned char)text[i] == 0x0C) { chunkStart = (long)i; break; }
        }
    }

    while (chunkStart >= 0) {
        size_t i;
        contentStart = chunkStart + 1;
        chunkEnd = -1;
        for (i = (size_t)contentStart; i < size; ++i) {
            if ((unsigned char)text[i] == 0x0C) { chunkEnd = (long)i; break; }
        }
        if (chunkEnd < 0)
            chunkEnd = (long)size;

        {
            char *chunk = aide_strndup(text + contentStart, (size_t)(chunkEnd - contentStart));
            AideRegexMatch m;
            if (aide_regex_exec(s_headerRe, chunk, 0, &m)) {
                AideAutodocEntry *entry = entryarray_push_new(out);
                entry->library = aide_strndup(chunk + m.start[1], (size_t)(m.end[1] - m.start[1]));
                entry->function = aide_strndup(chunk + m.start[2], (size_t)(m.end[2] - m.start[2]));
                entry->docFile = aide_strdup(path);
                entry->offset = contentStart;
                entry->length = chunkEnd - contentStart;
            }
            free(chunk);
        }

        chunkStart = (chunkEnd < (long)size) ? chunkEnd : -1;
    }

    free(text);
}

/* ---- JSON cache: "<cacheDir>/<sdkId>.json", keyed on file path ---- */

static char *cache_path(const char *cacheDir, const char *sdkId)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, cacheDir);
    aide_str_append_char(&s, '/');
    aide_str_append(&s, sdkId);
    aide_str_append(&s, ".json");
    return aide_str_take(&s);
}

typedef struct CacheEntry {
    char *path;
    long size;
    long long mtimeMs;
    EntryArray entries;
} CacheEntry;

typedef struct CacheMap {
    CacheEntry *items;
    size_t count;
    size_t cap;
} CacheMap;

static void cachemap_init(CacheMap *m) { m->items = NULL; m->count = 0; m->cap = 0; }

static void cachemap_free(CacheMap *m)
{
    size_t i;
    for (i = 0; i < m->count; ++i) {
        free(m->items[i].path);
        entryarray_free(&m->items[i].entries);
    }
    free(m->items);
    m->items = NULL;
    m->count = 0;
    m->cap = 0;
}

static CacheEntry *cachemap_find(CacheMap *m, const char *path)
{
    size_t i;
    for (i = 0; i < m->count; ++i) {
        if (strcmp(m->items[i].path, path) == 0)
            return &m->items[i];
    }
    return NULL;
}

static CacheEntry *cachemap_set(CacheMap *m, const char *path, long size, long long mtimeMs)
{
    CacheEntry *e = cachemap_find(m, path);
    if (e) {
        entryarray_free(&e->entries);
        entryarray_init(&e->entries);
        e->size = size;
        e->mtimeMs = mtimeMs;
        return e;
    }
    if (m->count >= m->cap) {
        size_t newCap = m->cap > 0 ? m->cap * 2 : 32;
        m->items = (CacheEntry *)realloc(m->items, newCap * sizeof(CacheEntry));
        m->cap = newCap;
    }
    e = &m->items[m->count++];
    e->path = aide_strdup(path);
    e->size = size;
    e->mtimeMs = mtimeMs;
    entryarray_init(&e->entries);
    return e;
}

static void load_cache(const char *cacheDir, const char *sdkId, CacheMap *out)
{
    char *path;
    char *json;
    size_t size;
    JSON_Value *root;
    JSON_Object *obj;
    size_t i, n;

    cachemap_init(out);
    if (cacheDir[0] == '\0')
        return;

    path = cache_path(cacheDir, sdkId);
    if (!aide_fs_read_file(path, &json, &size)) {
        free(path);
        return;
    }
    free(path);

    root = json_parse_string(json);
    free(json);
    if (!root || json_value_get_type(root) != JSONObject) {
        if (root) json_value_free(root);
        return;
    }
    obj = json_value_get_object(root);
    n = json_object_get_count(obj);
    for (i = 0; i < n; ++i) {
        const char *filePath = json_object_get_name(obj, i);
        JSON_Object *fileObj = json_value_get_object(json_object_get_value_at(obj, i));
        CacheEntry *ce;
        JSON_Array *entriesArr;
        size_t j, entryCount;

        if (!fileObj)
            continue;
        ce = cachemap_set(out, filePath,
                           (long)json_object_get_number(fileObj, "size"),
                           (long long)json_object_get_number(fileObj, "mtime"));

        entriesArr = json_object_get_array(fileObj, "entries");
        entryCount = entriesArr ? json_array_get_count(entriesArr) : 0;
        for (j = 0; j < entryCount; ++j) {
            JSON_Object *eObj = json_array_get_object(entriesArr, j);
            AideAutodocEntry *entry;
            const char *fn, *lib;
            if (!eObj)
                continue;
            entry = entryarray_push_new(&ce->entries);
            fn = json_object_get_string(eObj, "function");
            lib = json_object_get_string(eObj, "library");
            entry->function = aide_strdup(fn ? fn : "");
            entry->library = aide_strdup(lib ? lib : "");
            entry->docFile = aide_strdup(filePath);
            entry->offset = (long)json_object_get_number(eObj, "offset");
            entry->length = (long)json_object_get_number(eObj, "length");
        }
    }
    json_value_free(root);
}

static void save_cache(const char *cacheDir, const char *sdkId, const CacheMap *cache)
{
    JSON_Value *rootVal;
    JSON_Object *root;
    char *path;
    char *serialized;
    FILE *f;
    size_t i;

    if (cacheDir[0] == '\0')
        return;

    rootVal = json_value_init_object();
    root = json_value_get_object(rootVal);

    for (i = 0; i < cache->count; ++i) {
        const CacheEntry *ce = &cache->items[i];
        JSON_Value *fileVal = json_value_init_object();
        JSON_Object *fileObj = json_value_get_object(fileVal);
        JSON_Value *entriesVal = json_value_init_array();
        JSON_Array *entriesArr = json_value_get_array(entriesVal);
        size_t j;

        json_object_set_number(fileObj, "size", (double)ce->size);
        json_object_set_number(fileObj, "mtime", (double)ce->mtimeMs);
        for (j = 0; j < ce->entries.count; ++j) {
            const AideAutodocEntry *entry = &ce->entries.items[j];
            JSON_Value *eVal = json_value_init_object();
            JSON_Object *eObj = json_value_get_object(eVal);
            json_object_set_string(eObj, "function", entry->function);
            json_object_set_string(eObj, "library", entry->library);
            json_object_set_number(eObj, "offset", (double)entry->offset);
            json_object_set_number(eObj, "length", (double)entry->length);
            json_array_append_value(entriesArr, eVal);
        }
        json_object_set_value(fileObj, "entries", entriesVal);
        json_object_set_value(root, ce->path, fileVal);
    }

    aide_fs_mkpath(cacheDir);
    path = cache_path(cacheDir, sdkId);
    serialized = json_serialize_to_string(rootVal);
    f = fopen(path, "wb");
    if (f && serialized) {
        fwrite(serialized, 1, strlen(serialized), f);
        fclose(f);
    } else if (f) {
        fclose(f);
    }
    if (serialized)
        json_free_serialized_string(serialized);
    free(path);
    json_value_free(rootVal);
}

/* ---- index ---- */

void aide_autodocindex_init(AideAutodocIndex *idx, const char *cacheDir)
{
    idx->entries = NULL;
    idx->count = 0;
    idx->cap = 0;
    idx->cacheDir = aide_strdup(cacheDir ? cacheDir : "");
}

void aide_autodocindex_free(AideAutodocIndex *idx)
{
    size_t i;
    for (i = 0; i < idx->count; ++i)
        aide_autodocentry_free(&idx->entries[i]);
    free(idx->entries);
    idx->entries = NULL;
    idx->count = 0;
    idx->cap = 0;
    free(idx->cacheDir);
    idx->cacheDir = NULL;
}

static void index_push_copy(AideAutodocIndex *idx, const AideAutodocEntry *src)
{
    if (idx->count >= idx->cap) {
        size_t newCap = idx->cap > 0 ? idx->cap * 2 : 256;
        idx->entries = (AideAutodocEntry *)realloc(idx->entries, newCap * sizeof(AideAutodocEntry));
        idx->cap = newCap;
    }
    aide_autodocentry_copy(&idx->entries[idx->count], src);
    idx->count++;
}

static void scan_sdk(AideAutodocIndex *idx, const AideSdkDescriptor *sdk,
                      AideAutodocProgressFn progress, void *progressCtx)
{
    AideStrList files;
    CacheMap cache;
    int cacheDirty = 0;
    size_t i;

    aide_strlist_init(&files);
    for (i = 0; i < sdk->autodocFiles.count; ++i)
        aide_strlist_push(&files, sdk->autodocFiles.items[i]);
    for (i = 0; i < sdk->autodocDirs.count; ++i) {
        AideStrList names;
        size_t j;
        aide_strlist_init(&names);
        aide_fs_list_files(sdk->autodocDirs.items[i], ".doc", &names);
        for (j = 0; j < names.count; ++j) {
            char *full = aide_fs_join(sdk->autodocDirs.items[i], names.items[j]);
            aide_strlist_push_owned(&files, full);
        }
        aide_strlist_free(&names);
    }

    load_cache(idx->cacheDir, sdk->id, &cache);

    for (i = 0; i < files.count; ++i) {
        const char *path = files.items[i];
        long size;
        long long mtimeMs;
        CacheEntry *ce;
        size_t j;

        if (!aide_fs_stat(path, &size, &mtimeMs)) {
            if (progress)
                progress(progressCtx, sdk->id, i + 1, files.count);
            continue;
        }

        ce = cachemap_find(&cache, path);
        if (ce && ce->size == size && ce->mtimeMs == mtimeMs) {
            /* cache hit: reuse */
        } else {
            EntryArray scanned;
            entryarray_init(&scanned);
            scan_file(path, &scanned);
            ce = cachemap_set(&cache, path, size, mtimeMs);
            for (j = 0; j < scanned.count; ++j)
                entryarray_push_copy(&ce->entries, &scanned.items[j]);
            entryarray_free(&scanned);
            cacheDirty = 1;
        }

        for (j = 0; j < ce->entries.count; ++j)
            index_push_copy(idx, &ce->entries.items[j]);

        if (progress)
            progress(progressCtx, sdk->id, i + 1, files.count);
    }

    if (cacheDirty)
        save_cache(idx->cacheDir, sdk->id, &cache);

    cachemap_free(&cache);
    aide_strlist_free(&files);
}

void aide_autodocindex_reload(AideAutodocIndex *idx, const AideSdkDescriptor *activeSdks, size_t sdkCount,
                               AideAutodocProgressFn progress, void *progressCtx)
{
    size_t i;

    for (i = 0; i < idx->count; ++i)
        aide_autodocentry_free(&idx->entries[i]);
    idx->count = 0;

    for (i = 0; i < sdkCount; ++i)
        scan_sdk(idx, &activeSdks[i], progress, progressCtx);
}

static void find_exact(const AideAutodocIndex *idx, const char *functionName, EntryArray *out)
{
    size_t i;
    for (i = 0; i < idx->count; ++i) {
        if (strcmp(idx->entries[i].function, functionName) == 0)
            entryarray_push_copy(out, &idx->entries[i]);
    }
}

static void find_case_insensitive(const AideAutodocIndex *idx, const char *functionName, EntryArray *out)
{
    size_t i;
    for (i = 0; i < idx->count; ++i) {
        if (aide_str_ieq(idx->entries[i].function, functionName))
            entryarray_push_copy(out, &idx->entries[i]);
    }
}

void aide_autodocindex_find(const AideAutodocIndex *idx, const char *functionName,
                             AideAutodocEntry **outEntries, size_t *outCount)
{
    EntryArray result;
    entryarray_init(&result);

    find_exact(idx, functionName, &result);
    if (result.count == 0)
        find_case_insensitive(idx, functionName, &result);

    if (result.count == 0) {
        size_t len = strlen(functionName);
        static const char suffix[] = "Tags";
        size_t suffixLen = sizeof(suffix) - 1;
        if (len >= suffixLen && strcmp(functionName + (len - suffixLen), suffix) == 0) {
            AideStr alt;
            aide_str_init(&alt);
            aide_str_append_n(&alt, functionName, len - suffixLen);
            aide_str_append(&alt, "TagList");
            aide_autodocindex_find(idx, alt.data, outEntries, outCount);
            aide_str_free(&alt);
            entryarray_free(&result);
            return;
        }
    }

    if (result.count == 0) {
        *outEntries = NULL;
        *outCount = 0;
    } else {
        *outEntries = result.items;
        *outCount = result.count;
    }
}

void aide_autodocindex_find_wildcard_names(const AideAutodocIndex *idx, const char *pattern, AideStrList *out)
{
    char *ere = aide_regex_wildcard_to_ere(pattern);
    AideRegex *re = aide_regex_compile(ere, AIDE_REGEX_ICASE);
    size_t i;

    aide_strlist_clear(out);
    free(ere);
    if (!re)
        return;

    for (i = 0; i < idx->count; ++i) {
        const char *name = idx->entries[i].function;
        if (aide_regex_matches(re, name)) {
            size_t j;
            int dup = 0;
            for (j = 0; j < out->count; ++j) {
                if (strcmp(out->items[j], name) == 0) { dup = 1; break; }
            }
            if (!dup)
                aide_strlist_push(out, name);
        }
    }
    aide_regex_free(re);
}

char *aide_autodocindex_read_entry_text(const AideAutodocEntry *entry)
{
    char *text;
    size_t size;
    char *result;

    if (!aide_fs_read_file(entry->docFile, &text, &size))
        return aide_strdup("");

    if (entry->offset < 0 || (size_t)entry->offset > size) {
        free(text);
        return aide_strdup("");
    }
    {
        size_t avail = size - (size_t)entry->offset;
        size_t len = (entry->length < 0 || (size_t)entry->length > avail) ? avail : (size_t)entry->length;
        result = aide_strndup(text + entry->offset, len);
    }
    free(text);
    return result;
}

static int is_query_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '_' || c == '*' || c == '?';
}

static int is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

int aide_autodoc_query_from_selection(const char *selection, char *out, size_t outsz)
{
    const char *p;
    const char *q;
    size_t len = 0;
    size_t i;
    int hasName = 0;

    if (outsz == 0)
        return 0;
    out[0] = '\0';
    if (!selection)
        return 0;

    /* Only a '/' that starts a name splits: "dos.library/Open" does,
     * the '/' of a comment marker does not. */
    p = selection;
    for (q = selection; *q; ++q) {
        if (*q == '/' && is_ident_start(q[1]))
            p = q + 1;
    }

    while (*p && !is_query_char(*p))
        ++p;
    while (p[len] && is_query_char(p[len]) && len < outsz - 1)
        ++len;

    /* Wildcards alone would list every name in the index. */
    for (i = 0; i < len; ++i) {
        if (p[i] != '*' && p[i] != '?')
            hasName = 1;
    }
    if (!hasName)
        return 0;

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}
