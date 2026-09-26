#include "aide/aide_asmparser.h"
#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

static void includelist_push(AideAsmIncludeList *l, const char *path, int isIncdir)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 8;
        l->items = (AideAsmInclude *)realloc(l->items, newCap * sizeof(AideAsmInclude));
        l->cap = newCap;
    }
    l->items[l->count].path = aide_strdup(path);
    l->items[l->count].isIncdir = isIncdir;
    l->count++;
}

void aide_asmparser_result_init(AideAsmParseResult *r)
{
    aide_symbollist_init(&r->symbols);
    r->includes.items = NULL;
    r->includes.count = 0;
    r->includes.cap = 0;
}

void aide_asmparser_result_free(AideAsmParseResult *r)
{
    size_t i;
    aide_symbollist_free(&r->symbols);
    for (i = 0; i < r->includes.count; ++i)
        free(r->includes.items[i].path);
    free(r->includes.items);
    r->includes.items = NULL;
    r->includes.count = 0;
    r->includes.cap = 0;
}

static int is_word_c(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static int boundary_after(const char *line, long pos)
{
    return line[pos] == '\0' || !is_word_c(line[pos]);
}

static char *strip_semicolon_comment(const char *line, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (line[i] == ';')
            return aide_strndup(line, i);
    }
    return aide_strndup(line, len);
}

static AideRegex *s_globalLabelRe = NULL;
static AideRegex *s_localLabelRe = NULL;
static AideRegex *s_equRe = NULL;
static AideRegex *s_macroRe = NULL;
static AideRegex *s_includeRe = NULL;

static void ensure_compiled(void)
{
    if (!s_globalLabelRe)
        s_globalLabelRe = aide_regex_compile("^([A-Za-z_][A-Za-z0-9_$]*)[[:space:]]*:?", 0);
    if (!s_localLabelRe)
        s_localLabelRe = aide_regex_compile(
            "^(\\.[A-Za-z0-9_$]+|[A-Za-z_][A-Za-z0-9_$]*\\$)[[:space:]]*:?", 0);
    if (!s_equRe)
        s_equRe = aide_regex_compile(
            "^[[:space:]]*([A-Za-z0-9_.$]+)[[:space:]]+(equ|=|set)[[:space:]]+[^[:space:]]", AIDE_REGEX_ICASE);
    if (!s_macroRe)
        s_macroRe = aide_regex_compile("^[[:space:]]*([A-Za-z0-9_.$]+)[[:space:]]+macro", AIDE_REGEX_ICASE);
    if (!s_includeRe)
        s_includeRe = aide_regex_compile(
            "^[[:space:]]*(include|incdir)[[:space:]]+\"?([^\"[:space:];]+)\"?", AIDE_REGEX_ICASE);
}

void aide_asmparser_parse(const char *text, const char *absoluteFilePath, AideAsmParseResult *out)
{
    size_t textLen = strlen(text);
    size_t lineStart = 0;
    size_t i;
    int lineNumber = 0;
    AideStr currentScope;

    ensure_compiled();
    aide_asmparser_result_init(out);
    aide_str_init(&currentScope);

    for (i = 0; i <= textLen; ++i) {
        if (i != textLen && text[i] != '\n')
            continue;

        {
            size_t rawLen = i - lineStart;
            char *line = strip_semicolon_comment(text + lineStart, rawLen);
            char *trimmed;
            AideRegexMatch m;
            int startsWithSpace;
            int handled = 0;

            ++lineNumber;
            trimmed = aide_str_trim_dup(line);

            if (trimmed[0] == '*') {
                free(line);
                free(trimmed);
                lineStart = i + 1;
                continue;
            }

            if (aide_regex_exec(s_includeRe, line, 0, &m)) {
                char *pathText = aide_strndup(line + m.start[2], (size_t)(m.end[2] - m.start[2]));
                char *kw = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                includelist_push(&out->includes, pathText, aide_str_ieq(kw, "incdir"));
                free(pathText);
                free(kw);
                handled = 1;
            }

            startsWithSpace = line[0] != '\0' && (line[0] == ' ' || line[0] == '\t');

            if (!handled && aide_regex_exec(s_macroRe, line, 0, &m) && boundary_after(line, m.end[0])) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_ASM_MACRO;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
                handled = 1;
            }
            if (!handled && aide_regex_exec(s_equRe, line, 0, &m)) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_EQU_CONSTANT;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
                handled = 1;
            }
            if (!handled && aide_regex_exec(s_localLabelRe, trimmed, 0, &m)) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(trimmed + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_LOCAL_LABEL;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
                free(s->scope); s->scope = aide_strdup(currentScope.data);
                handled = 1;
            }
            if (!handled && !startsWithSpace && aide_regex_exec(s_globalLabelRe, line, 0, &m)) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_LABEL;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
                aide_str_clear(&currentScope);
                aide_str_append(&currentScope, s->name);
            }

            free(line);
            free(trimmed);
        }

        lineStart = i + 1;
    }

    aide_str_free(&currentScope);
}
