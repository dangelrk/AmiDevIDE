#ifndef AIDE_SEARCH_H
#define AIDE_SEARCH_H

/* Portable C99 mirror of search/ProjectSearch.h -- line-oriented text
 * search across a set of files. Backs both "Find in Project..." and
 * "Find All References". */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideSearchMatch {
    char *file;      /* absolute path */
    int line;         /* 1-based */
    int column;       /* 0-based, start of the match within lineText */
    char *lineText;   /* full line, for display in the results dock */
    int isDefinition; /* set by the caller; search() itself never sets this */
} AideSearchMatch;

typedef struct AideSearchMatchList {
    AideSearchMatch *items;
    size_t count;
    size_t cap;
} AideSearchMatchList;

void aide_search_matchlist_init(AideSearchMatchList *l);
void aide_search_matchlist_free(AideSearchMatchList *l);

/* `pattern` is a literal substring unless `regex` is nonzero, in which
 * case it's used as a POSIX ERE directly. `wholeWord` requires a word
 * boundary on both sides of each match (checked manually against the
 * surrounding characters, since POSIX ERE has no \b -- functionally
 * equivalent to wrapping the pattern in \b...\b). Files that can't be
 * opened are silently skipped. Fills *out (cleared first) with every
 * match, in file order then line order. */
void aide_search_search(const AideStrList *files, const char *pattern, int caseSensitive, int wholeWord,
                         int regex, AideSearchMatchList *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_SEARCH_H */
