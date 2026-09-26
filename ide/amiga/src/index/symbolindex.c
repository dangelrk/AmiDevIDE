#include "index/symbolindex.h"

#include "aide/aide_asmparser.h"
#include "aide/aide_cparser.h"
#include "aide/aide_fs.h"

#include <stdlib.h>
#include <string.h>

static int has_suffix(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcmp(s + ls - lf, suffix) == 0;
}

static int is_asm_path(const char *path)
{
    return has_suffix(path, ".s") || has_suffix(path, ".asm") || has_suffix(path, ".i");
}

static void append_symbols(AideSymbolList *out, const AideSymbolList *src)
{
    size_t i;
    for (i = 0; i < src->count; ++i) {
        AideSymbol *dst = aide_symbollist_push_new(out);
        aide_symbol_copy(dst, &src->items[i]);
    }
}

static void parse_one_file(const char *absPath, AideSymbolList *out)
{
    char *data;
    size_t size;

    if (!aide_fs_read_file(absPath, &data, &size))
        return;

    if (is_asm_path(absPath)) {
        AideAsmParseResult r;
        aide_asmparser_result_init(&r);
        aide_asmparser_parse(data, absPath, &r);
        append_symbols(out, &r.symbols);
        aide_asmparser_result_free(&r);
    } else {
        AideCParseResult r;
        aide_cparser_result_init(&r);
        aide_cparser_parse(data, absPath, &r);
        append_symbols(out, &r.symbols);
        aide_cparser_result_free(&r);
    }
    free(data);
}

void symbolindex_rebuild(AideSymbolList *out, const AideStrList *absoluteSources,
                          const AideStrList *absoluteHeaders)
{
    size_t i;

    aide_symbollist_free(out);
    for (i = 0; i < absoluteSources->count; ++i)
        parse_one_file(absoluteSources->items[i], out);
    for (i = 0; i < absoluteHeaders->count; ++i)
        parse_one_file(absoluteHeaders->items[i], out);
}

const AideSymbol *symbolindex_find_definition(const AideSymbolList *syms, const char *name)
{
    const AideSymbol *fallback = NULL;
    size_t i;

    for (i = 0; i < syms->count; ++i) {
        const AideSymbol *s = &syms->items[i];
        if (strcmp(s->name, name) != 0)
            continue;
        if (s->kind == AIDE_SYM_PROTOTYPE) {
            if (!fallback) fallback = s;
            continue;
        }
        return s;
    }
    return fallback;
}
