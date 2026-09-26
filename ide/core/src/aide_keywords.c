#include "aide/aide_keywords.h"

static const char *const kKeywords[] = {
    /* C keywords */
    "auto", "break", "case", "const", "continue", "default", "do", "double",
    "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int",
    "long", "register", "restrict", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while",
    /* Amiga C types */
    "APTR", "BPTR", "UBYTE", "BYTE", "UWORD", "WORD", "ULONG", "LONG",
    "STRPTR", "CONST_STRPTR", "BOOL", "Tag", "TagItem",
    /* m68k directives / common mnemonics */
    "section", "dc", "ds", "equ", "set", "rs", "macro", "endm",
    "xdef", "xref", "incbin", "include", "incdir", "org", "even",
    "move", "movea", "movem", "lea", "add", "sub", "cmp", "bra", "bsr",
    "jmp", "jsr", "rts", "nop", "trap",
};

static const size_t kKeywordCount = sizeof(kKeywords) / sizeof(kKeywords[0]);

const char *const *aide_keywords_static(size_t *outCount)
{
    *outCount = kKeywordCount;
    return kKeywords;
}
