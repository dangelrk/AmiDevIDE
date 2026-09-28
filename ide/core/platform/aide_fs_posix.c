/* Host backend for aide_fs.h: POSIX stat()/realpath(). Compiled only for
 * the qt/ (macOS/Linux) build; the Amiga build compiles
 * amiga/src/mui/aide_fs_amiga.c instead (dos.library-backed, A1
 * milestone). */

#include "aide/aide_fs.h"
#include "aide/aide_str.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int aide_fs_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
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
    char buf[PATH_MAX];
    if (!realpath(path, buf))
        return NULL;
    return aide_strdup(buf);
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

int aide_fs_read_range(const char *path, long offset, size_t maxBytes,
                        char **outData, size_t *outSize)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t readCount;

    if (!f)
        return 0;
    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    buf = (char *)malloc(maxBytes + 1);
    readCount = fread(buf, 1, maxBytes, f);
    fclose(f);
    buf[readCount] = '\0';

    *outData = buf;
    *outSize = readCount;
    return 1;
}

int aide_fs_mkpath(const char *path)
{
    char buf[PATH_MAX];
    size_t len = strlen(path);
    size_t i;

    if (len == 0 || len >= sizeof(buf))
        return 0;
    memcpy(buf, path, len + 1);

    for (i = 1; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    mkdir(buf, 0755);
    return aide_fs_exists(path);
}

int aide_fs_stat(const char *path, long *outSize, long long *outMtimeMs)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    *outSize = (long)st.st_size;
    *outMtimeMs = (long long)st.st_mtime * 1000;
    return 1;
}

void aide_fs_list_files(const char *dirPath, const char *suffix, AideStrList *out)
{
    DIR *dir = opendir(dirPath);
    struct dirent *entry;
    size_t suffixLen = strlen(suffix);

    if (!dir)
        return;

    while ((entry = readdir(dir)) != NULL) {
        size_t nameLen = strlen(entry->d_name);
        if (nameLen < suffixLen)
            continue;
        if (strcmp(entry->d_name + (nameLen - suffixLen), suffix) != 0)
            continue;

        {
            /* Skip directories (and anything else not a regular file). */
            char *fullPath = aide_fs_join(dirPath, entry->d_name);
            struct stat st;
            if (stat(fullPath, &st) == 0 && S_ISREG(st.st_mode))
                aide_strlist_push(out, entry->d_name);
            free(fullPath);
        }
    }
    closedir(dir);
}

void aide_fs_list_dirs(const char *dirPath, AideStrList *out)
{
    DIR *dir = opendir(dirPath);
    struct dirent *entry;

    if (!dir)
        return;

    while ((entry = readdir(dir)) != NULL) {
        char *fullPath;
        struct stat st;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        fullPath = aide_fs_join(dirPath, entry->d_name);
        if (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode))
            aide_strlist_push(out, entry->d_name);
        free(fullPath);
    }
    closedir(dir);
}
