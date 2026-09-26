/* Host backend for aide_regex.h: the system POSIX <regex.h> (present in
 * macOS's and Linux's libc, hence no vendoring needed on the host side).
 * Compiled only for the qt/ (macOS/Linux/Windows-via-MSYS) build; the
 * Amiga build compiles amiga/src/mui/aide_regex_amiga.c instead, which
 * implements the exact same aide_regex.h against regex.library. */

#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

struct AideRegex {
    regex_t re;
};

AideRegex *aide_regex_compile(const char *pattern, int flags)
{
    AideRegex *r = (AideRegex *)malloc(sizeof(AideRegex));
    int cflags = REG_EXTENDED;
    char *expanded = aide_regex_expand_shorthand(pattern);
    int rc;

    if (flags & AIDE_REGEX_ICASE)
        cflags |= REG_ICASE;
    if (flags & AIDE_REGEX_NOSUB)
        cflags |= REG_NOSUB;

    rc = regcomp(&r->re, expanded, cflags);
    free(expanded);
    if (rc != 0) {
        free(r);
        return NULL;
    }
    return r;
}

void aide_regex_free(AideRegex *re)
{
    if (!re)
        return;
    regfree(&re->re);
    free(re);
}

int aide_regex_exec(const AideRegex *re, const char *text, size_t from,
                     AideRegexMatch *match)
{
    regmatch_t groups[AIDE_REGEX_MAX_GROUPS];
    size_t nmatch = AIDE_REGEX_MAX_GROUPS;
    int i;
    int rc;

    rc = regexec(&re->re, text + from, nmatch, groups, 0);
    if (rc != 0)
        return 0;

    match->group_count = 0;
    for (i = 0; i < AIDE_REGEX_MAX_GROUPS; ++i) {
        if (groups[i].rm_so < 0) {
            match->start[i] = -1;
            match->end[i] = -1;
            continue;
        }
        match->start[i] = (long)from + (long)groups[i].rm_so;
        match->end[i] = (long)from + (long)groups[i].rm_eo;
        match->group_count = i + 1;
    }
    return 1;
}

int aide_regex_matches(const AideRegex *re, const char *text)
{
    AideRegexMatch m;
    return aide_regex_exec(re, text, 0, &m);
}

/* aide_regex_escape() / aide_regex_wildcard_to_ere() are pure string
 * manipulation (no library calls), so they live once in
 * core/src/aide_regex_common.c and are shared by every backend instead
 * of being duplicated here and in aide_regex_amiga.c. */
