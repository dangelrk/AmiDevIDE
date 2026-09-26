#include "aide/aide_emulator.h"
#include "aide/aide_fs.h"
#include "aide/aide_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define AIDE_EMU_WINDOWS 1
#elif defined(__M68K__) || defined(AMIGA)
#  define AIDE_EMU_AMIGA 1
#else
#  define AIDE_EMU_UNIX 1
#endif

#ifndef AIDE_EMU_AMIGA
#  define AIDE_EMU_HAS_POPEN 1
#endif

/* ------------------------------------------------------------------ */

static int copy_out(const char *value, char *out, size_t out_size)
{
    size_t n = strlen(value);
    if (n + 1 > out_size)
        return 0;
    memcpy(out, value, n + 1);
    return 1;
}

int aide_emulator_default_config_path(const char *toolchain_root,
                                      char *out, size_t out_size)
{
    AideStr s;
    int ok;

    if (!toolchain_root || !out || out_size == 0)
        return 0;

    aide_str_init(&s);
    aide_str_append(&s, toolchain_root);
    /* append_path knows that an AmigaDOS assign already ends in ':' and must
     * not get a separator inserted after it. */
    aide_str_append_path(&s, "/fs-uae/development.fs-uae");
    ok = copy_out(s.data, out, out_size);
    aide_str_free(&s);

    if (!ok && out_size)
        out[0] = '\0';
    return ok;
}

/* ------------------------------------------------------------------ */

#ifdef AIDE_EMU_HAS_POPEN

/* Candidate install locations, most specific first.  PATH is consulted after
 * these because a user with both a bundle and a package manager copy almost
 * always means the bundle. */
static const char *const kCandidates[] = {
#if defined(__APPLE__)
    "/Applications/FS-UAE.app/Contents/MacOS/fs-uae",
    "/opt/homebrew/bin/fs-uae",
    "/usr/local/bin/fs-uae",
#elif defined(AIDE_EMU_WINDOWS)
    "C:/Program Files/FS-UAE/Windows/x86-64/fs-uae.exe",
    "C:/Program Files/FS-UAE/fs-uae.exe",
    "C:/Program Files (x86)/FS-UAE/Windows/x86/fs-uae.exe",
#else
    "/usr/bin/fs-uae",
    "/usr/local/bin/fs-uae",
    "/snap/bin/fs-uae",
#endif
    NULL
};

static int search_path(const char *name, char *out, size_t out_size)
{
    const char *path = getenv("PATH");
    const char *p;
#ifdef AIDE_EMU_WINDOWS
    const char sep = ';';
#else
    const char sep = ':';
#endif

    if (!path)
        return 0;

    for (p = path; *p; ) {
        const char *end = strchr(p, sep);
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0) {
            AideStr s;
            int found;
            aide_str_init(&s);
            aide_str_append_n(&s, p, len);
            aide_str_append_path(&s, name);
            found = aide_fs_exists(s.data);
            if (found)
                found = copy_out(s.data, out, out_size);
            aide_str_free(&s);
            if (found)
                return 1;
        }
        if (!end)
            break;
        p = end + 1;
    }
    return 0;
}

int aide_emulator_find_binary(char *out, size_t out_size)
{
    size_t i;

    if (!out || out_size == 0)
        return 0;
    out[0] = '\0';

    for (i = 0; kCandidates[i]; ++i) {
        if (aide_fs_exists(kCandidates[i]))
            return copy_out(kCandidates[i], out, out_size);
    }

#ifdef AIDE_EMU_WINDOWS
    return search_path("fs-uae.exe", out, out_size);
#else
    return search_path("fs-uae", out, out_size);
#endif
}

int aide_emulator_is_running(const char *exe_name)
{
    char command[512];
    FILE *pipe;
    int saw_output = 0;
    char line[512];

    if (!exe_name || !exe_name[0])
        return 0;

    /* Reject anything that could escape the shell word rather than trying to
     * quote it: this is a process name, so the character set is tiny. */
    {
        const char *c;
        for (c = exe_name; *c; ++c) {
            int ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
                     || (*c >= '0' && *c <= '9')
                     || *c == '-' || *c == '_' || *c == '.';
            if (!ok)
                return 0;
        }
    }

#ifdef AIDE_EMU_WINDOWS
    /* tasklist prints a header and a "no tasks" notice on stderr; the filter
     * means a matching line is the only thing that reaches stdout. */
    snprintf(command, sizeof command,
             "tasklist /FI \"IMAGENAME eq %s\" /NH 2>NUL", exe_name);
#else
    snprintf(command, sizeof command, "pgrep -x %s 2>/dev/null", exe_name);
#endif

#ifdef AIDE_EMU_WINDOWS
    pipe = _popen(command, "r");
#else
    pipe = popen(command, "r");
#endif
    if (!pipe)
        return 0;

    while (fgets(line, sizeof line, pipe)) {
#ifdef AIDE_EMU_WINDOWS
        /* With /NH the only stdout line is a real match; tasklist otherwise
         * says "INFO: No tasks are running..." which we filter out. */
        if (strstr(line, exe_name))
            saw_output = 1;
#else
        if (line[0] >= '0' && line[0] <= '9')
            saw_output = 1;
#endif
    }

#ifdef AIDE_EMU_WINDOWS
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return saw_output;
}

#else /* AIDE_EMU_AMIGA */

/* Running natively on AmigaOS there is no emulator in the picture, and vbcc's
 * runtime has no popen().  Both queries answer honestly rather than being
 * compiled out, so callers need no #ifdef of their own. */

int aide_emulator_find_binary(char *out, size_t out_size)
{
    if (out && out_size)
        out[0] = '\0';
    return 0;
}

/* exe_name is deliberately unused: there is no process list to search. It is
   left unreferenced rather than cast to void because vbcc reports the usual
   `(void)x;` idiom as warning 153, "statement has no effect", and this branch
   is only ever compiled by vbcc. */
int aide_emulator_is_running(const char *exe_name)
{
    return 0;
}

#endif
