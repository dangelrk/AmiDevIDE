#include "aide/aide_symbol.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

void aide_symbol_init(AideSymbol *s)
{
    s->name = aide_strdup("");
    s->kind = AIDE_SYM_FUNCTION;
    s->file = aide_strdup("");
    s->line = 0;
    s->signature = aide_strdup("");
    s->scope = aide_strdup("");
    s->offset = -1;
}

void aide_symbol_free(AideSymbol *s)
{
    free(s->name);
    free(s->file);
    free(s->signature);
    free(s->scope);
    s->name = s->file = s->signature = s->scope = NULL;
}

void aide_symbol_copy(AideSymbol *dst, const AideSymbol *src)
{
    dst->name = aide_strdup(src->name);
    dst->kind = src->kind;
    dst->file = aide_strdup(src->file);
    dst->line = src->line;
    dst->signature = aide_strdup(src->signature);
    dst->scope = aide_strdup(src->scope);
    dst->offset = src->offset;
}

void aide_symbollist_init(AideSymbolList *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void aide_symbollist_free(AideSymbolList *l)
{
    size_t i;
    for (i = 0; i < l->count; ++i)
        aide_symbol_free(&l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

AideSymbol *aide_symbollist_push_new(AideSymbolList *l)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 16;
        l->items = (AideSymbol *)realloc(l->items, newCap * sizeof(AideSymbol));
        l->cap = newCap;
    }
    aide_symbol_init(&l->items[l->count]);
    return &l->items[l->count++];
}
