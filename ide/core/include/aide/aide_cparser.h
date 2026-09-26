#ifndef AIDE_CPARSER_H
#define AIDE_CPARSER_H

/* Portable C99 mirror of index/CSymbolParser.h -- lightweight, line-
 * oriented C parser: strips comments and string/char literals first,
 * tracks brace depth. Extracts #define, typedef (both forms),
 * struct/union/enum tags (with member offsets via aide_structlayout),
 * function prototypes/definitions at depth 0, and #include directives.
 * See that header for the full false-positive-tolerance rationale. */

#include "aide/aide_symbol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideCInclude {
    char *directive; /* the path text between <> or "" */
    int quoted;       /* 1 for "...", 0 for <...> */
} AideCInclude;

typedef struct AideCIncludeList {
    AideCInclude *items;
    size_t count;
    size_t cap;
} AideCIncludeList;

typedef struct AideCParseResult {
    AideSymbolList symbols;
    AideCIncludeList includes;
} AideCParseResult;

void aide_cparser_result_init(AideCParseResult *r);
void aide_cparser_result_free(AideCParseResult *r);

void aide_cparser_parse(const char *text, const char *absoluteFilePath, AideCParseResult *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_CPARSER_H */
