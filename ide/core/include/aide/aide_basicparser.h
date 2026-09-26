#ifndef AIDE_BASICPARSER_H
#define AIDE_BASICPARSER_H

/* Line-oriented ACE BASIC parser for the symbol outline, built the same
 * way aide_asmparser is: no real grammar, just the declaration forms that
 * are recognisable from the start of a line.
 *
 * Recognised (see the ACE repo's docs/ref.txt):
 *   SUB / FUNCTION / METHOD name        -> AIDE_SYM_FUNCTION
 *   DECLARE SUB|FUNCTION name           -> AIDE_SYM_PROTOTYPE
 *   DECLARE STRUCT name                 -> AIDE_SYM_STRUCT
 *   CONST name = ...                    -> AIDE_SYM_DEFINE
 *   DIM / SHARED / typed declarations   -> AIDE_SYM_VARIABLE
 *   label:                              -> AIDE_SYM_LABEL
 *
 * #include directives are reported separately so the include graph can
 * use them, mirroring aide_asmparser's include list.
 */

#include "aide/aide_symbol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideBasicIncludeList {
    char **items;
    size_t count;
    size_t cap;
} AideBasicIncludeList;

typedef struct AideBasicParseResult {
    AideSymbolList symbols;
    AideBasicIncludeList includes;
} AideBasicParseResult;

void aide_basicparser_result_init(AideBasicParseResult *r);
void aide_basicparser_result_free(AideBasicParseResult *r);

void aide_basicparser_parse(const char *text, const char *absoluteFilePath,
                            AideBasicParseResult *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_BASICPARSER_H */
