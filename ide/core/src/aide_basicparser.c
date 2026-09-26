#include "aide/aide_basicparser.h"
#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

static void includelist_push(AideBasicIncludeList *l, const char *path)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 8;
        l->items = (char **)realloc(l->items, newCap * sizeof(char *));
        l->cap = newCap;
    }
    l->items[l->count++] = aide_strdup(path);
}

void aide_basicparser_result_init(AideBasicParseResult *r)
{
    aide_symbollist_init(&r->symbols);
    r->includes.items = NULL;
    r->includes.count = 0;
    r->includes.cap = 0;
}

void aide_basicparser_result_free(AideBasicParseResult *r)
{
    size_t i;
    aide_symbollist_free(&r->symbols);
    for (i = 0; i < r->includes.count; ++i)
        free(r->includes.items[i]);
    free(r->includes.items);
    r->includes.items = NULL;
    r->includes.count = 0;
    r->includes.cap = 0;
}

/*
 * ACE has four comment forms: REM and ' to end of line, and the block
 * forms {...} and C-style. Only the two line forms matter for outlining;
 * a declaration is never followed by a block comment that would change
 * how its head parses. Quotes are respected so an apostrophe inside a
 * string literal does not truncate the line.
 */
static char *strip_line_comment(const char *line, size_t len)
{
    size_t i;
    int inString = 0;

    for (i = 0; i < len; ++i) {
        char c = line[i];
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (inString)
            continue;
        if (c == '\'')
            return aide_strndup(line, i);
        if (c == '/' && i + 1 < len && line[i + 1] == '/')
            return aide_strndup(line, i);
    }
    return aide_strndup(line, len);
}

static AideRegex *s_subRe = NULL;
static AideRegex *s_declSubRe = NULL;
static AideRegex *s_declStructRe = NULL;
static AideRegex *s_structDefRe = NULL;
static AideRegex *s_structEndRe = NULL;
static AideRegex *s_memberRe = NULL;
static AideRegex *s_constRe = NULL;
static AideRegex *s_dimRe = NULL;
static AideRegex *s_labelRe = NULL;
static AideRegex *s_includeRe = NULL;
static AideRegex *s_remRe = NULL;

/* An ACE identifier may carry a type suffix: $ & ! % # */
#define BAS_IDENT "([A-Za-z_][A-Za-z0-9_]*[$&!%#]?)"

static void ensure_compiled(void)
{
    if (s_subRe)
        return;

    /* "SUB Name", "FUNCTION Name", "METHOD Name", each optionally
     * preceded by a return type (SUB STRING Get$, METHOD Draw). */
    s_subRe = aide_regex_compile(
        "^[[:space:]]*(SUB|FUNCTION|METHOD)[[:space:]]+"
        "((SHORTINT|LONGINT|SINGLE|STRING|ADDRESS)[[:space:]]+)?" BAS_IDENT,
        AIDE_REGEX_ICASE);

    s_declSubRe = aide_regex_compile(
        "^[[:space:]]*DECLARE[[:space:]]+(SUB|FUNCTION)[[:space:]]+"
        "((SHORTINT|LONGINT|SINGLE|STRING|ADDRESS)[[:space:]]+)?" BAS_IDENT,
        AIDE_REGEX_ICASE);

    /* "STRUCT Name" opens a type definition, closed by "END STRUCT"
     * (ref.txt: STRUCT). Everything between is a member. */
    s_structDefRe = aide_regex_compile(
        "^[[:space:]]*STRUCT[[:space:]]+" BAS_IDENT, AIDE_REGEX_ICASE);
    s_structEndRe = aide_regex_compile(
        "^[[:space:]]*END[[:space:]]+STRUCT[[:space:]]*$", AIDE_REGEX_ICASE);

    /* "DECLARE STRUCT Type [*] name" declares VARIABLES of an existing
     * type, not a type (ref.txt line 587) -- the name to show is the last
     * identifier, not the type. */
    s_declStructRe = aide_regex_compile(
        "^[[:space:]]*DECLARE[[:space:]]+STRUCT[[:space:]]+" BAS_IDENT
        "[[:space:]]+\\*?[[:space:]]*" BAS_IDENT,
        AIDE_REGEX_ICASE);

    /* A struct member is "TYPE name" indented inside a definition. */
    s_memberRe = aide_regex_compile(
        "^[[:space:]]*(SHORTINT|LONGINT|SINGLE|STRING|ADDRESS|BYTE|WORD)[[:space:]]+" BAS_IDENT,
        AIDE_REGEX_ICASE);

    s_constRe = aide_regex_compile(
        "^[[:space:]]*CONST[[:space:]]+" BAS_IDENT "[[:space:]]*=",
        AIDE_REGEX_ICASE);

    /* DIM/SHARED plus the bare typed declarations ACE also accepts
     * (SHORTINT n, STRING s SIZE 40, ADDRESS p, ...). */
    s_dimRe = aide_regex_compile(
        "^[[:space:]]*(DIM|SHARED|SHORTINT|LONGINT|SINGLE|STRING|ADDRESS)[[:space:]]+"
        "(SHARED[[:space:]]+)?" BAS_IDENT,
        AIDE_REGEX_ICASE);

    /* A label is an identifier alone on its line followed by ':'. */
    s_labelRe = aide_regex_compile("^([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*:[[:space:]]*$", 0);

    s_includeRe = aide_regex_compile(
        "^[[:space:]]*#[[:space:]]*include[[:space:]]+[\"<]([^\">]+)[\">]", AIDE_REGEX_ICASE);

    /* REM comments out the rest of the line, but only as a whole word --
     * "REMOVE" is an identifier, not a comment. */
    s_remRe = aide_regex_compile("^[[:space:]]*REM([[:space:]].*)?$", AIDE_REGEX_ICASE);
}

static void push_symbol(AideBasicParseResult *out, const char *text, const AideRegexMatch *m,
                        int group, AideSymbolKind kind, const char *file, int line)
{
    AideSymbol *s = aide_symbollist_push_new(&out->symbols);
    free(s->name);
    s->name = aide_strndup(text + m->start[group], (size_t)(m->end[group] - m->start[group]));
    s->kind = kind;
    free(s->file);
    s->file = aide_strdup(file);
    s->line = line;
}

void aide_basicparser_parse(const char *text, const char *absoluteFilePath,
                            AideBasicParseResult *out)
{
    size_t textLen = strlen(text);
    size_t lineStart = 0;
    size_t i;
    int lineNumber = 0;
    AideStr structScope;   /* name of the STRUCT definition we are inside */

    ensure_compiled();
    aide_basicparser_result_init(out);
    aide_str_init(&structScope);

    for (i = 0; i <= textLen; ++i) {
        size_t rawLen;
        char *line;
        AideRegexMatch m;

        if (i != textLen && text[i] != '\n')
            continue;

        rawLen = i - lineStart;
        /* tolerate CRLF sources */
        if (rawLen > 0 && text[lineStart + rawLen - 1] == '\r')
            --rawLen;
        line = strip_line_comment(text + lineStart, rawLen);
        ++lineNumber;

        if (aide_regex_exec(s_remRe, line, 0, &m)) {
            /* whole line is a REM comment */
        } else if (aide_regex_exec(s_includeRe, line, 0, &m)) {
            char *path = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
            includelist_push(&out->includes, path);
            free(path);
        } else if (structScope.len > 0 && aide_regex_exec(s_structEndRe, line, 0, &m)) {
            aide_str_clear(&structScope);
        } else if (structScope.len > 0 && aide_regex_exec(s_memberRe, line, 0, &m)) {
            AideSymbol *sym;
            push_symbol(out, line, &m, 2, AIDE_SYM_STRUCT_MEMBER, absoluteFilePath, lineNumber);
            sym = &out->symbols.items[out->symbols.count - 1];
            free(sym->scope);
            sym->scope = aide_strdup(structScope.data);
            free(sym->signature);
            sym->signature = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
        } else if (aide_regex_exec(s_declStructRe, line, 0, &m)) {
            AideSymbol *sym;
            push_symbol(out, line, &m, 2, AIDE_SYM_VARIABLE, absoluteFilePath, lineNumber);
            sym = &out->symbols.items[out->symbols.count - 1];
            free(sym->signature);
            sym->signature = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
        } else if (aide_regex_exec(s_structDefRe, line, 0, &m)) {
            push_symbol(out, line, &m, 1, AIDE_SYM_STRUCT, absoluteFilePath, lineNumber);
            aide_str_clear(&structScope);
            aide_str_append(&structScope, out->symbols.items[out->symbols.count - 1].name);
        } else if (aide_regex_exec(s_declSubRe, line, 0, &m)) {
            push_symbol(out, line, &m, 4, AIDE_SYM_PROTOTYPE, absoluteFilePath, lineNumber);
        } else if (aide_regex_exec(s_subRe, line, 0, &m)) {
            push_symbol(out, line, &m, 4, AIDE_SYM_FUNCTION, absoluteFilePath, lineNumber);
        } else if (aide_regex_exec(s_constRe, line, 0, &m)) {
            push_symbol(out, line, &m, 1, AIDE_SYM_DEFINE, absoluteFilePath, lineNumber);
        } else if (aide_regex_exec(s_dimRe, line, 0, &m)) {
            push_symbol(out, line, &m, 3, AIDE_SYM_VARIABLE, absoluteFilePath, lineNumber);
        } else if (aide_regex_exec(s_labelRe, line, 0, &m)) {
            push_symbol(out, line, &m, 1, AIDE_SYM_LABEL, absoluteFilePath, lineNumber);
        }

        free(line);
        lineStart = i + 1;
    }

    aide_str_free(&structScope);
}
