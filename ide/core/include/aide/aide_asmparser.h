#ifndef AIDE_ASMPARSER_H
#define AIDE_ASMPARSER_H

/* Portable C99 mirror of index/AsmSymbolParser.h -- line-oriented m68k
 * asm parser: column-0 global labels, local .x/x$ labels scoped to the
 * preceding global label, equ/=/set, macro definitions, and
 * include/incdir directives. */

#include "aide/aide_symbol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideAsmInclude {
    char *path;
    int isIncdir; /* incdir entries are search-path additions, not direct file includes */
} AideAsmInclude;

typedef struct AideAsmIncludeList {
    AideAsmInclude *items;
    size_t count;
    size_t cap;
} AideAsmIncludeList;

typedef struct AideAsmParseResult {
    AideSymbolList symbols;
    AideAsmIncludeList includes;
} AideAsmParseResult;

void aide_asmparser_result_init(AideAsmParseResult *r);
void aide_asmparser_result_free(AideAsmParseResult *r);

void aide_asmparser_parse(const char *text, const char *absoluteFilePath, AideAsmParseResult *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_ASMPARSER_H */
