#ifndef AIDE_AUTODOC_H
#define AIDE_AUTODOC_H

/* Portable C99 mirror of help/AutodocIndex.h -- scans the autodocDirs/
 * autodocFiles of every active SDK for formfeed-separated "library/
 * function" autodoc entries (Latin-1; byte offset == char offset since
 * Latin-1 is single-byte), with a JSON cache per SDK id keyed on
 * (path, size, mtime) so an SDK selection change only rescans changed
 * files. See that header for the exact lookup fallback chain. */

#include "aide/aide_sdk.h"
#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideAutodocEntry {
    char *function;
    char *library;
    char *docFile;
    long offset;
    long length;
} AideAutodocEntry;

void aide_autodocentry_free(AideAutodocEntry *e);
void aide_autodocentry_copy(AideAutodocEntry *dst, const AideAutodocEntry *src);

typedef struct AideAutodocIndex {
    AideAutodocEntry *entries;
    size_t count;
    size_t cap;
    char *cacheDir; /* base dir for "<cacheDir>/<sdkId>.json"; "" disables caching */
} AideAutodocIndex;

/* `cacheDir` may be "" to disable the on-disk cache (every reload()
 * rescans every file); otherwise it's created on demand. */
void aide_autodocindex_init(AideAutodocIndex *idx, const char *cacheDir);
void aide_autodocindex_free(AideAutodocIndex *idx);

/* Optional progress callback -- called after each autodoc file is
 * scanned/cache-hit, fileIndex 1-based out of fileCount, so a slow UI
 * (real 68k hardware, first scan of an uncached SDK) can show something
 * better than an indefinite hang. Either pointer may be NULL to disable. */
typedef void (*AideAutodocProgressFn)(void *ctx, const char *sdkId, size_t fileIndex, size_t fileCount);

void aide_autodocindex_reload(AideAutodocIndex *idx, const AideSdkDescriptor *activeSdks, size_t sdkCount,
                               AideAutodocProgressFn progress, void *progressCtx);

/* Fills *outEntries (freshly malloc'd array of freshly copied entries,
 * *outCount long; free each with aide_autodocentry_free() then free()
 * the array) with every match for `functionName`, via the
 * exact -> case-insensitive -> "...Tags"->"...TagList" fallback chain.
 * *outCount is 0 (and *outEntries NULL) if nothing matched. */
void aide_autodocindex_find(const AideAutodocIndex *idx, const char *functionName,
                             AideAutodocEntry **outEntries, size_t *outCount);

/* Fills *out with every distinct indexed function name matching the
 * `*`/`?` wildcard `pattern` (case-insensitive), deduplicated. */
void aide_autodocindex_find_wildcard_names(const AideAutodocIndex *idx, const char *pattern, AideStrList *out);

/* Reads the entry's text slice from its source file. Freshly malloc'd;
 * "" if the file can't be read. */
char *aide_autodocindex_read_entry_text(const AideAutodocEntry *entry);

/* Turns text the user marked in the help pane into a help query: the
 * first run of identifier or `*`/`?` wildcard characters after the last
 * '/' ("dos.library/Open()" -> "Open", " AllocMem(size," -> "AllocMem").
 * Writes at most outsz-1 chars plus NUL to `out`; returns 0 (and an
 * empty `out`) when the selection holds no such run. */
int aide_autodoc_query_from_selection(const char *selection, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_AUTODOC_H */
