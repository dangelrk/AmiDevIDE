#ifndef AIDE_MUI_SYMBOLINDEX_H
#define AIDE_MUI_SYMBOLINDEX_H

/* Project-wide symbol database for the MUI IDE -- parses every project
 * source/header with aide_cparser/aide_asmparser (picked by file
 * extension) and concatenates the results into one AideSymbolList, the
 * same shape the Qt side's SymbolDatabase/SymbolsDock work from. Backs
 * the "Symbole" panel and F3 go-to-definition. */

#include "aide/aide_str.h"
#include "aide/aide_symbol.h"

/* Frees *out's previous contents, then re-parses every absolute path in
 * `absoluteSources` and `absoluteHeaders`, appending every symbol found. */
void symbolindex_rebuild(AideSymbolList *out, const AideStrList *absoluteSources,
                          const AideStrList *absoluteHeaders);

/* Returns the "best" definition for `name`: prefers an actual definition
 * (Function/Variable/Define/Typedef/Struct/Union/Enum/Label/EquConstant/
 * AsmMacro) over a bare Prototype, falling back to the first prototype
 * match if that's all there is. NULL if nothing matches. */
const AideSymbol *symbolindex_find_definition(const AideSymbolList *syms, const char *name);

#endif /* AIDE_MUI_SYMBOLINDEX_H */
