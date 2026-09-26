#ifndef AIDE_SYMBOL_H
#define AIDE_SYMBOL_H

/* Portable C99 mirror of index/Symbol.h -- one parsed C/asm symbol. Shared
 * by aide_cparser.c, aide_asmparser.c and (later) a portable symbol
 * database, so the Amiga/MUI side gets the exact same symbol model the Qt
 * side's SymbolsDock/go-to-definition already rely on. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AideSymbolKind {
    AIDE_SYM_FUNCTION = 0,
    AIDE_SYM_PROTOTYPE,
    AIDE_SYM_VARIABLE,
    AIDE_SYM_DEFINE,
    AIDE_SYM_TYPEDEF,
    AIDE_SYM_STRUCT,
    AIDE_SYM_UNION,
    AIDE_SYM_ENUM,
    AIDE_SYM_LABEL,
    AIDE_SYM_LOCAL_LABEL,
    AIDE_SYM_EQU_CONSTANT,
    AIDE_SYM_ASM_MACRO,
    AIDE_SYM_STRUCT_MEMBER,
} AideSymbolKind;

typedef struct AideSymbol {
    char *name;
    AideSymbolKind kind;
    char *file;      /* absolute path */
    int line;
    char *signature; /* tooltip text; for a StructMember, its declared type */
    char *scope;     /* local asm labels: enclosing global label; StructMember: owning struct/union/typedef name */
    int offset;      /* Struct/Union/Typedef: total size once known (-1 until then); StructMember: byte offset */
} AideSymbol;

/* Sets every field to its default (empty strings, offset -1, kind Function). */
void aide_symbol_init(AideSymbol *s);
void aide_symbol_free(AideSymbol *s);
/* Deep-copies src into dst (dst need not be initialized first). */
void aide_symbol_copy(AideSymbol *dst, const AideSymbol *src);

typedef struct AideSymbolList {
    AideSymbol *items;
    size_t count;
    size_t cap;
} AideSymbolList;

void aide_symbollist_init(AideSymbolList *l);
void aide_symbollist_free(AideSymbolList *l);
/* Appends a new, zero-initialized (via aide_symbol_init) slot and returns
 * a pointer to it for the caller to fill in directly (avoids an extra
 * copy for the common "build it in place" pattern). */
AideSymbol *aide_symbollist_push_new(AideSymbolList *l);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_SYMBOL_H */
