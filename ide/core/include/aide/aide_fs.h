#ifndef AIDE_FS_H
#define AIDE_FS_H

/* Minimal filesystem shim used by core/ modules that need to check for a
 * file's existence or build an absolute path (IncludeGraph::resolveInclude
 * today; AutodocIndex's mtime/size cache keys later). Two backends:
 *
 *   - core/platform/aide_fs_posix.c: stat()/realpath() (macOS/Linux/qt/ build)
 *   - amiga/src/mui/aide_fs_amiga.c (A1 milestone, not yet written): the
 *     same header against dos.library (Lock/Examine/NameFromLock).
 *
 * Every function here takes/returns plain '/'-separated paths -- the
 * AmigaDOS ':'/'/'  translation, if ever needed, stays local to the
 * Amiga backend. */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True if a file or directory exists at `path`. */
int aide_fs_exists(const char *path);

/* Joins `dir` and `relative` ("dir/relative"), or returns a copy of
 * `relative` unchanged if it's already absolute (starts with '/').
 * Freshly malloc'd; caller frees(). */
char *aide_fs_join(const char *dir, const char *relative);

/* Resolves `path` to a canonical absolute form (symlinks/./.. resolved).
 * Returns NULL if `path` doesn't exist. Freshly malloc'd. */
char *aide_fs_canonical(const char *path);

/* Reads the whole file at `path` into a freshly malloc'd buffer
 * (NUL-terminated for convenience; *outSize excludes that trailing NUL,
 * so it's safe on files with embedded NUL bytes too). Returns 1 on
 * success, 0 if the file couldn't be opened. */
int aide_fs_read_file(const char *path, char **outData, size_t *outSize);

/* Appends the names (not full paths) of every regular file directly
 * inside `dirPath` whose name ends with `suffix` (plain suffix match,
 * e.g. ".sfd" or ".doc" -- case-sensitive, no glob syntax) to *out.
 * Silently does nothing if `dirPath` can't be opened as a directory. */
void aide_fs_list_files(const char *dirPath, const char *suffix, AideStrList *out);

/* Fills *outSize (bytes) and *outMtimeMs (milliseconds since epoch,
 * whole-second resolution is fine -- used only for cache staleness
 * checks) for the file at `path`. Returns 1 on success, 0 if it
 * doesn't exist. */
int aide_fs_stat(const char *path, long *outSize, long long *outMtimeMs);

/* Creates `path` and every missing parent directory (like `mkdir -p`).
 * Returns 1 on success (including "already exists"). */
int aide_fs_mkpath(const char *path);

/* Reads up to maxBytes starting at byte `offset` into a freshly malloc'd
 * buffer (NUL-terminated for convenience, *outSize excludes that
 * trailing NUL). Returns 1 on success -- including a 0-byte read at
 * EOF, which is not an error -- 0 if the file couldn't be opened.
 *
 * Opened in SHARED read mode: another process may still be appending to
 * the same file (the build log tailer's use case, see mainwindow.c's
 * build_tick()) -- that is the whole point of this function over
 * aide_fs_read_file(), which reopens/rereads the entire file every
 * call. The Amiga backend uses Open(MODE_OLDFILE) explicitly rather
 * than vclib's fopen(), because MODE_OLDFILE's shared-access semantics
 * are the property this function exists for, not an implementation
 * detail to leave to whatever fopen() happens to map to. */
int aide_fs_read_range(const char *path, long offset, size_t maxBytes,
                        char **outData, size_t *outSize);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_FS_H */
