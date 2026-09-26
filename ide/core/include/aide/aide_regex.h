#ifndef AIDE_REGEX_H
#define AIDE_REGEX_H

/* Thin POSIX-1003.2 regex facade (regcomp/regexec/regfree) used by every
 * regex-bearing core/ module (aide_cparser, aide_asmparser, aide_sfd,
 * aide_structlayout, aide_diag, aide_search, aide_autodoc).
 *
 * Host build (macOS/Linux): backed directly by the system <regex.h>.
 * Amiga build: backed by regex.library through hand-written vbcc LVO stubs in
 * amiga/src/mui/regex_stubs.c, because regex.library's headers are
 * SAS/C `#pragma libcall` style and vbcc silently ignores those. Only
 * this header's implementation file changes between the two builds;
 * every caller in core/ is identical on both sides.
 *
 * Only the POSIX ERE subset is used anywhere in this codebase (no
 * lookaround, no backreferences) so both backends behave identically. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIDE_REGEX_ICASE   0x1 /* case-insensitive match */
#define AIDE_REGEX_NOSUB   0x2 /* don't bother capturing groups */

#define AIDE_REGEX_MAX_GROUPS 10

typedef struct AideRegex AideRegex;

typedef struct AideRegexMatch {
    /* Byte offsets into the string passed to aide_regex_exec(), or
     * (-1,-1) for a group that didn't participate. Group 0 is the
     * whole match. */
    long start[AIDE_REGEX_MAX_GROUPS];
    long end[AIDE_REGEX_MAX_GROUPS];
    int group_count; /* how many of start/end are valid */
} AideRegexMatch;

/* Expands the common Perl/PCRE shorthand classes (\w \W \d \D \s \S)
 * into their POSIX bracket-expression equivalents outside of any
 * existing [...] bracket expression, leaving every other escape (\.
 * \( \) \+ \\ ...) untouched. Backend-independent (core/src/
 * aide_regex_common.c) so both the host <regex.h> and Amiga
 * regex.library backends -- neither of which understands \w-family
 * shorthand, both being plain POSIX-1003.2 -- get it for free; every
 * aide_regex_compile() implementation calls this on `pattern` first.
 * Freshly malloc'd; caller frees(). */
char *aide_regex_expand_shorthand(const char *pattern);

/* Compiles an extended POSIX regex (after running it through
 * aide_regex_expand_shorthand() -- so \w/\d/\s *are* supported, unlike
 * plain POSIX ERE). Returns NULL on a bad pattern. Otherwise ERE rules
 * apply: character classes ([[:alpha:]] etc.), anchors, captures via
 * (), *, +, ?, {n,m}, | -- no \b, no lookaround (translate those into
 * character classes / manual scans at the call site instead). */
AideRegex *aide_regex_compile(const char *pattern, int flags);
void aide_regex_free(AideRegex *re);

/* Matches starting at or after byte offset `from` in `text` (which
 * must be NUL-terminated). Returns 1 and fills *match on success, 0
 * on no match. */
int aide_regex_exec(const AideRegex *re, const char *text, size_t from,
                     AideRegexMatch *match);

/* Convenience: true if `re` matches anywhere in `text`. */
int aide_regex_matches(const AideRegex *re, const char *text);

/* Escapes every ERE metacharacter in `text` so the result matches it
 * literally when compiled; writes into a fresh malloc'd buffer the
 * caller must free(). Mirrors QRegularExpression::escape(). */
char *aide_regex_escape(const char *text);

/* Translates a `*`/`?` shell-style wildcard pattern into an ERE
 * anchored to the whole string (^...$), malloc'd. Mirrors
 * QRegularExpression::wildcardToRegularExpression() for the subset
 * actually used here (no character classes in the wildcard itself). */
char *aide_regex_wildcard_to_ere(const char *wildcard);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_REGEX_H */
