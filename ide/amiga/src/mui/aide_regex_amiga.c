/* Amiga backend for core/include/aide/aide_regex.h: regex.library
 * (POSIX 1003.2, Henry Spencer's implementation). regex.library's headers are
 * SAS/C `#pragma libcall` style, which vbcc silently ignores (produces
 * plain undefined-symbol link errors, not a compile error) -- exactly the
 * muimaster.library problem mui_stubs.h documents, solved the same way:
 * hand-written vbcc-native inline LVO stubs, offsets/registers taken
 * from sdk/regex/fd/regex_lib.fd (compiler-independent ABI facts, not
 * tied to any pragma dialect):
 *
 *   ##bias 30           -- dispatch (private) at -30
 *   regcomp (a0,a1,d0)  -- preg,pattern,cflags   -> LVO -36 (0x24)
 *   regerror(a1,d2,d0/d1) -- unused by this API, stub omitted
 *   regexec (a1,d0,d1,d2,d3) -- preg,string,nmatch,pmatch,flags -> LVO -48 (0x30)
 *   regfree (a0)        -- preg                  -> LVO -54 (0x36)
 *
 * regex_t/regmatch_t are redefined locally rather than including
 * sdk/regex/include/libraries/regex.h verbatim: that header pulls in
 * <sys/dir.h> for off_t, which isn't part of vbcc's vclib. The layout
 * below matches the real library's ABI exactly (same fields, same
 * order) -- verified against sdk/regex/include/libraries/regex.h. */

#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <exec/types.h>
#include <exec/libraries.h>

#include <stdlib.h>
#include <string.h>

typedef long aide_regoff_t;

typedef struct {
    int re_magic;
    unsigned long re_nsub;
    const char *re_endp;
    void *re_g;
} AideRegexT;

typedef struct {
    aide_regoff_t rm_so;
    aide_regoff_t rm_eo;
} AideRegmatchT;

#define AIDE_REG_EXTENDED 0001
#define AIDE_REG_ICASE    0002

/* Opened once at startup by amiga/src/app/main.c (OpenLibrary("regex.library", 0)),
 * exactly like MUIMasterBase/IntuitionBase in mui_stubs.h. Every
 * aide_regex_compile() call fails gracefully (returns NULL) if this is
 * still NULL -- callers already handle a failed compile. */
extern struct Library *RegexBase;

static int __regcomp(__reg("a6") struct Library *, __reg("a0") AideRegexT *,
                      __reg("a1") const char *, __reg("d0") int) = "\tjsr\t-36(a6)";
static int __regexec(__reg("a6") struct Library *, __reg("a1") const AideRegexT *,
                      __reg("d0") const char *, __reg("d1") unsigned long,
                      __reg("d2") AideRegmatchT *, __reg("d3") int) = "\tjsr\t-48(a6)";
static void __regfree(__reg("a6") struct Library *, __reg("a0") AideRegexT *) = "\tjsr\t-54(a6)";

struct AideRegex {
    AideRegexT re;
};

AideRegex *aide_regex_compile(const char *pattern, int flags)
{
    AideRegex *r;
    int cflags = AIDE_REG_EXTENDED;
    char *expanded;
    int rc;

    if (!RegexBase)
        return NULL;

    if (flags & AIDE_REGEX_ICASE)
        cflags |= AIDE_REG_ICASE;
    /* AIDE_REGEX_NOSUB has no documented regex.library equivalent;
     * every caller passes nmatch=AIDE_REGEX_MAX_GROUPS to regexec()
     * regardless, so this is a missed micro-optimization at worst, not
     * a correctness issue. */

    r = (AideRegex *)malloc(sizeof(AideRegex));
    if (!r)
        return NULL;

    expanded = aide_regex_expand_shorthand(pattern);
    rc = __regcomp(RegexBase, &r->re, expanded, cflags);
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
    __regfree(RegexBase, &re->re);
    free(re);
}

int aide_regex_exec(const AideRegex *re, const char *text, size_t from, AideRegexMatch *match)
{
    AideRegmatchT groups[AIDE_REGEX_MAX_GROUPS];
    int rc, i;

    memset(groups, 0, sizeof(groups));
    rc = __regexec(RegexBase, &re->re, text + from, AIDE_REGEX_MAX_GROUPS, groups, 0);
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
