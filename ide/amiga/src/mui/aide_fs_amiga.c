/* Amiga backend for core/include/aide/aide_fs.h: dos.library
 * (Lock/Examine/ExNext/NameFromLock/CreateDir), using the classic
 * Lock()+Examine()+ExNext() idiom.
 *
 * aide_fs_read_file() is one exception: it just uses stdio (fopen/
 * fread), which vbcc's vclib implements fully -- no need to hand-roll an
 * Open()/Read()/Close() version when the portable one already works
 * unchanged. aide_fs_read_range() is the other exception,
 * but in the opposite direction: it explicitly does NOT use fopen(),
 * because its whole reason to exist is MODE_OLDFILE's shared-read
 * access while another process is still appending to the same file
 * (the build log tailer) -- that must not be an accident of whatever
 * fopen() happens to map to underneath, so it goes straight to
 * Open()/Seek()/Read()/Close(). */

#include "aide/aide_fs.h"
#include "aide/aide_str.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AmigaDOS has no "." for "current directory" (unlike POSIX) -- Lock()
 * would look for a literal file named ".". The documented convention is
 * an empty string ("Lock(\"\", ...)" locks the current directory). None
 * of core/'s callers currently pass "." here, but this keeps the
 * behavior correct if one ever does. */
static const char *amiga_path(const char *path)
{
    return (path[0] == '.' && path[1] == '\0') ? "" : path;
}

int aide_fs_exists(const char *path)
{
    BPTR lock = Lock((CONST_STRPTR)amiga_path(path), ACCESS_READ);
    if (!lock)
        return 0;
    UnLock(lock);
    return 1;
}

char *aide_fs_join(const char *dir, const char *relative)
{
    AideStr s;
    if (relative[0] == '/')
        return aide_strdup(relative);
    aide_str_init(&s);
    aide_str_append(&s, dir);
    aide_str_append_char(&s, '/');
    aide_str_append(&s, relative);
    return aide_str_take(&s);
}

char *aide_fs_canonical(const char *path)
{
    BPTR lock = Lock((CONST_STRPTR)amiga_path(path), ACCESS_READ);
    char buf[256];
    char *result;

    if (!lock)
        return NULL;
    if (!NameFromLock(lock, (STRPTR)buf, sizeof(buf))) {
        UnLock(lock);
        return NULL;
    }
    UnLock(lock);
    result = aide_strdup(buf);
    return result;
}

int aide_fs_read_file(const char *path, char **outData, size_t *outSize)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    size_t readCount;

    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return 0;
    }

    buf = (char *)malloc((size_t)size + 1);
    readCount = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[readCount] = '\0';

    *outData = buf;
    *outSize = readCount;
    return 1;
}

void aide_fs_list_files(const char *dirPath, const char *suffix, AideStrList *out)
{
    BPTR lock = Lock((CONST_STRPTR)amiga_path(dirPath), ACCESS_READ);
    struct FileInfoBlock *fib;
    size_t suffixLen = strlen(suffix);

    if (!lock)
        return;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return;
    }
    if (!Examine(lock, fib)) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        return;
    }

    while (ExNext(lock, fib)) {
        size_t nameLen;
        if (fib->fib_DirEntryType >= 0) /* directory, not a plain file */
            continue;
        nameLen = strlen(fib->fib_FileName);
        if (nameLen < suffixLen)
            continue;
        if (strcmp(fib->fib_FileName + (nameLen - suffixLen), suffix) != 0)
            continue;
        aide_strlist_push(out, fib->fib_FileName);
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
}

void aide_fs_list_dirs(const char *dirPath, AideStrList *out)
{
    BPTR lock = Lock((CONST_STRPTR)amiga_path(dirPath), ACCESS_READ);
    struct FileInfoBlock *fib;

    if (!lock)
        return;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return;
    }
    if (Examine(lock, fib)) {
        /* ExNext() lists the contents only, never the directory itself,
         * and AmigaDOS has no "." or ".." entries to skip. */
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType > 0)
                aide_strlist_push(out, fib->fib_FileName);
        }
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
}

int aide_fs_stat(const char *path, long *outSize, long long *outMtimeMs)
{
    BPTR lock = Lock((CONST_STRPTR)amiga_path(path), ACCESS_READ);
    struct FileInfoBlock *fib;
    int ok = 0;

    if (!lock)
        return 0;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (fib) {
        if (Examine(lock, fib)) {
            /* Not a real epoch -- just a monotonically comparable value
             * (whole ms since the AmigaDOS DateStamp origin) for the
             * cache-staleness equality/inequality checks in
             * core/src/aide_autodoc.c, which never interprets it as a
             * calendar date. ds_Tick is in 1/50s units = 20ms each. */
            *outSize = (fib->fib_DirEntryType >= 0) ? 0L : (long)fib->fib_Size;
            *outMtimeMs = (long long)fib->fib_Date.ds_Days * 86400000LL
                        + (long long)fib->fib_Date.ds_Minute * 60000LL
                        + (long long)fib->fib_Date.ds_Tick * 20LL;
            ok = 1;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return ok;
}

int aide_fs_read_range(const char *path, long offset, size_t maxBytes,
                        char **outData, size_t *outSize)
{
    BPTR fh = Open((CONST_STRPTR)amiga_path(path), MODE_OLDFILE);
    char *buf;
    LONG n;

    if (!fh)
        return 0;
    if (Seek(fh, offset, OFFSET_BEGINNING) < 0) {
        Close(fh);
        return 0;
    }

    buf = (char *)malloc(maxBytes + 1);
    n = Read(fh, buf, (LONG)maxBytes);
    Close(fh);
    if (n < 0) {
        free(buf);
        return 0;
    }
    buf[n] = '\0';

    *outData = buf;
    *outSize = (size_t)n;
    return 1;
}

int aide_fs_mkpath(const char *path)
{
    char buf[256];
    size_t len = strlen(path);
    size_t i;

    if (len == 0 || len >= sizeof(buf))
        return 0;
    memcpy(buf, path, len + 1);

    for (i = 1; i < len; ++i) {
        if (buf[i] == '/') {
            BPTR lock;
            buf[i] = '\0';
            lock = CreateDir((CONST_STRPTR)buf);
            if (lock)
                UnLock(lock);
            buf[i] = '/';
        }
    }
    {
        BPTR lock = CreateDir((CONST_STRPTR)buf);
        if (lock)
            UnLock(lock);
    }
    return aide_fs_exists(path);
}
