#include "aide/aide_cparser.h"
#include "aide/aide_hash.h"
#include "aide/aide_regex.h"
#include "aide/aide_str.h"
#include "aide/aide_structlayout.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- small shared helpers ---- */

static int is_space_c(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static int is_word_c(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* ---- includes list ---- */

static void includelist_init(AideCIncludeList *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static void includelist_free(AideCIncludeList *l)
{
    size_t i;
    for (i = 0; i < l->count; ++i)
        free(l->items[i].directive);
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static void includelist_push(AideCIncludeList *l, const char *directive, int quoted)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 8;
        l->items = (AideCInclude *)realloc(l->items, newCap * sizeof(AideCInclude));
        l->cap = newCap;
    }
    l->items[l->count].directive = aide_strdup(directive);
    l->items[l->count].quoted = quoted;
    l->count++;
}

void aide_cparser_result_init(AideCParseResult *r)
{
    aide_symbollist_init(&r->symbols);
    includelist_init(&r->includes);
}

void aide_cparser_result_free(AideCParseResult *r)
{
    aide_symbollist_free(&r->symbols);
    includelist_free(&r->includes);
}

/* ---- line splitting (mirrors QString::split('\n'): '\r' stays attached) ---- */

typedef struct LineRef {
    const char *start;
    size_t len;
} LineRef;

typedef struct LineRefList {
    LineRef *items;
    size_t count;
    size_t cap;
} LineRefList;

static void linereflist_push(LineRefList *l, const char *start, size_t len)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 32;
        l->items = (LineRef *)realloc(l->items, newCap * sizeof(LineRef));
        l->cap = newCap;
    }
    l->items[l->count].start = start;
    l->items[l->count].len = len;
    l->count++;
}

static void split_lines_ref(const char *text, LineRefList *out)
{
    size_t n = strlen(text);
    size_t lineStart = 0;
    size_t i;
    out->items = NULL;
    out->count = 0;
    out->cap = 0;
    for (i = 0; i <= n; ++i) {
        if (i == n || text[i] == '\n') {
            linereflist_push(out, text + lineStart, i - lineStart);
            lineStart = i + 1;
        }
    }
}

/* ---- strip line comments and block comments and "..."/'...' literal contents,
 * replacing literal/block-comment bodies with spaces so column positions
 * stay stable (// comments truncate the line instead, matching the Qt
 * original exactly). Tracks block-comment state across lines. ---- */

static void strip_comments_and_strings(const LineRefList *rawLines, AideStrList *out)
{
    int inBlockComment = 0;
    size_t li;

    aide_strlist_init(out);
    for (li = 0; li < rawLines->count; ++li) {
        const char *line = rawLines->items[li].start;
        size_t n = rawLines->items[li].len;
        AideStr s;
        size_t j = 0;
        aide_str_init(&s);

        while (j < n) {
            char c = line[j];
            char c2 = (j + 1 < n) ? line[j + 1] : '\0';

            if (inBlockComment) {
                if (j + 1 < n && c == '*' && c2 == '/') {
                    inBlockComment = 0;
                    aide_str_append(&s, "  ");
                    j += 2;
                } else {
                    aide_str_append_char(&s, ' ');
                    ++j;
                }
                continue;
            }
            if (j + 1 < n && c == '/' && c2 == '/')
                break; /* rest of line is a comment */
            if (j + 1 < n && c == '/' && c2 == '*') {
                inBlockComment = 1;
                aide_str_append(&s, "  ");
                j += 2;
                continue;
            }
            if (c == '"' || c == '\'') {
                char quote = c;
                aide_str_append_char(&s, quote);
                ++j;
                while (j < n && line[j] != quote) {
                    if (line[j] == '\\' && j + 1 < n) {
                        aide_str_append(&s, "  ");
                        j += 2;
                    } else {
                        aide_str_append_char(&s, ' ');
                        ++j;
                    }
                }
                if (j < n) {
                    aide_str_append_char(&s, quote);
                    ++j;
                }
                continue;
            }
            aide_str_append_char(&s, c);
            ++j;
        }
        aide_strlist_push_owned(out, aide_str_take(&s));
    }
}

/* ---- hand-scanned replacements for the two lazy-quantifier Qt patterns
 * (POSIX ERE has no `*?`/`+?`) ----
 *
 * try_match_function_decl mirrors:
 *   ^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\(([^;{}]*)\)\s*(\{)?\s*;?\s*$
 * The lazy prefix, worked through by hand (see the module's design notes),
 * always settles on "the identifier run immediately before the '(',
 * skipping trailing whitespace, requiring at least one prefix character
 * before it" -- i.e. plain greedy right-to-left scanning gives the same
 * result as the original's lazy left-to-right one for every case that
 * can appear in real C source. */
static int try_match_function_decl(const char *line, char **outName, char **outParams, int *outHasBrace)
{
    size_t len = strlen(line);
    size_t headEnd, i;
    size_t nameEnd, nameStart;
    size_t boundary;
    size_t closeParen;
    size_t tail;

    if (len == 0)
        return 0;
    if (!(isalpha((unsigned char)line[0]) || line[0] == '_'))
        return 0;

    headEnd = (size_t)-1;
    for (i = 1; i < len; ++i) {
        if (line[i] == '(') { headEnd = i; break; }
    }
    if (headEnd == (size_t)-1)
        return 0;

    for (i = 1; i < headEnd; ++i) {
        char c = line[i];
        if (!(is_word_c(c) || c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' || c == '*'))
            return 0;
    }

    nameEnd = headEnd;
    while (nameEnd > 0 && (line[nameEnd - 1] == ' ' || line[nameEnd - 1] == '\t'))
        --nameEnd;
    nameStart = nameEnd;
    while (nameStart > 0 && is_word_c(line[nameStart - 1]))
        --nameStart;
    if (nameStart == 0 || nameStart == nameEnd)
        return 0;

    boundary = len;
    for (i = headEnd + 1; i < len; ++i) {
        if (line[i] == ';' || line[i] == '{' || line[i] == '}') { boundary = i; break; }
    }
    closeParen = (size_t)-1;
    for (i = headEnd + 1; i < boundary; ++i) {
        if (line[i] == ')')
            closeParen = i;
    }
    if (closeParen == (size_t)-1)
        return 0;

    tail = closeParen + 1;
    while (tail < len && is_space_c(line[tail]))
        ++tail;
    *outHasBrace = 0;
    if (tail < len && line[tail] == '{') {
        *outHasBrace = 1;
        ++tail;
        while (tail < len && is_space_c(line[tail]))
            ++tail;
    }
    if (tail < len && line[tail] == ';') {
        ++tail;
        while (tail < len && is_space_c(line[tail]))
            ++tail;
    }
    if (tail != len)
        return 0;

    *outName = aide_strndup(line + nameStart, nameEnd - nameStart);
    *outParams = aide_strndup(line + headEnd + 1, closeParen - (headEnd + 1));
    return 1;
}

/* try_match_funcptr_decl mirrors:
 *   ^\s*(.+?)\(\*\s*(\w+)\s*\)\s*\([^()]*\)\s*$
 * Tries every "(*" occurrence left to right (emulating lazy backtracking)
 * until the remainder validates fully. */
static int try_match_funcptr_decl(const char *decl, char **outPrefix, char **outName)
{
    size_t len = strlen(decl);
    size_t leadStart = 0;
    size_t i;

    while (leadStart < len && is_space_c(decl[leadStart]))
        ++leadStart;

    for (i = leadStart; i + 1 < len; ++i) {
        size_t p, nameStart, nameEnd;

        if (!(decl[i] == '(' && decl[i + 1] == '*'))
            continue;
        if (i <= leadStart) /* group1 (.+?) requires >=1 char */
            continue;

        p = i + 2;
        while (p < len && is_space_c(decl[p]))
            ++p;
        nameStart = p;
        while (p < len && is_word_c(decl[p]))
            ++p;
        if (p == nameStart)
            continue;
        nameEnd = p;

        while (p < len && is_space_c(decl[p]))
            ++p;
        if (p >= len || decl[p] != ')')
            continue;
        ++p;
        while (p < len && is_space_c(decl[p]))
            ++p;
        if (p >= len || decl[p] != '(')
            continue;
        ++p;
        while (p < len && decl[p] != '(' && decl[p] != ')')
            ++p;
        if (p >= len || decl[p] != ')')
            continue;
        ++p;
        while (p < len && is_space_c(decl[p]))
            ++p;
        if (p != len)
            continue;

        *outPrefix = aide_strndup(decl + leadStart, i - leadStart);
        *outName = aide_strndup(decl + nameStart, nameEnd - nameStart);
        return 1;
    }
    return 0;
}

/* ---- struct/union member declaration parsing ---- */

typedef struct PendingMember {
    char *type;
    char *name;
    int line;
} PendingMember;

typedef struct PendingMemberList {
    PendingMember *items;
    size_t count;
    size_t cap;
} PendingMemberList;

static void pendinglist_init(PendingMemberList *l) { l->items = NULL; l->count = 0; l->cap = 0; }

static void pendinglist_clear(PendingMemberList *l)
{
    size_t i;
    for (i = 0; i < l->count; ++i) { free(l->items[i].type); free(l->items[i].name); }
    l->count = 0;
}

static void pendinglist_free(PendingMemberList *l)
{
    pendinglist_clear(l);
    free(l->items);
    l->items = NULL;
    l->cap = 0;
}

static void pendinglist_push(PendingMemberList *l, const char *type, const char *name, int lineNumber)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 8;
        l->items = (PendingMember *)realloc(l->items, newCap * sizeof(PendingMember));
        l->cap = newCap;
    }
    l->items[l->count].type = aide_strdup(type);
    l->items[l->count].name = aide_strdup(name);
    l->items[l->count].line = lineNumber;
    l->count++;
}

static size_t count_ws_tokens(const char *text)
{
    size_t n = strlen(text), i = 0, count = 0;
    while (i < n) {
        while (i < n && is_space_c(text[i])) ++i;
        if (i >= n) break;
        while (i < n && !is_space_c(text[i])) ++i;
        ++count;
    }
    return count;
}

/* Parses one struct/union body line (already comment/string-stripped)
 * into zero or more PendingMembers -- see aide_cparser.h's Qt
 * counterpart for the exact declarator-splitting rules this implements
 * (comma-separated lists, per-declarator leading '*'s, array suffixes,
 * simple function-pointer members). */
static void parse_member_decl_line(const char *line, int lineNumber, PendingMemberList *members)
{
    const char *semi = strchr(line, ';');
    char *decl, *trimmed;
    char *funcPrefix, *funcName;
    size_t totalTokens, n, i, tok0start, tok0len;
    int typeTokenCount;
    size_t idx;
    int tokensSeen;
    AideStr baseType;
    char *declaratorPartRaw, *declaratorPart;

    if (!semi)
        return;
    decl = aide_strndup(line, (size_t)(semi - line));
    trimmed = aide_str_trim_dup(decl);
    if (trimmed[0] == '\0') {
        free(decl);
        free(trimmed);
        return;
    }

    if (try_match_funcptr_decl(decl, &funcPrefix, &funcName)) {
        char *trimmedPrefix = aide_str_trim_dup(funcPrefix);
        AideStr type;
        aide_str_init(&type);
        aide_str_append(&type, trimmedPrefix);
        aide_str_append(&type, " (*)()");
        pendinglist_push(members, type.data, funcName, lineNumber);
        aide_str_free(&type);
        free(trimmedPrefix);
        free(funcPrefix);
        free(funcName);
        free(decl);
        free(trimmed);
        return;
    }

    totalTokens = count_ws_tokens(trimmed);
    if (totalTokens == 0) {
        free(decl);
        free(trimmed);
        return;
    }

    n = strlen(trimmed);
    i = 0;
    while (i < n && is_space_c(trimmed[i])) ++i;
    tok0start = i;
    while (i < n && !is_space_c(trimmed[i])) ++i;
    tok0len = i - tok0start;

    typeTokenCount = 1;
    if ((tok0len == 6 && strncmp(trimmed + tok0start, "struct", 6) == 0)
        || (tok0len == 5 && strncmp(trimmed + tok0start, "union", 5) == 0)
        || (tok0len == 4 && strncmp(trimmed + tok0start, "enum", 4) == 0))
        typeTokenCount = 2;

    if (totalTokens <= (size_t)typeTokenCount) {
        free(decl);
        free(trimmed);
        return;
    }

    idx = 0;
    tokensSeen = 0;
    aide_str_init(&baseType);
    while (tokensSeen < typeTokenCount && idx < n) {
        size_t ts;
        while (idx < n && is_space_c(trimmed[idx])) ++idx;
        ts = idx;
        while (idx < n && !is_space_c(trimmed[idx])) ++idx;
        if (tokensSeen > 0)
            aide_str_append_char(&baseType, ' ');
        aide_str_append_n(&baseType, trimmed + ts, idx - ts);
        ++tokensSeen;
    }

    declaratorPartRaw = aide_strndup(trimmed + idx, n - idx);
    declaratorPart = aide_str_trim_dup(declaratorPartRaw);
    free(declaratorPartRaw);

    if (declaratorPart[0] != '\0') {
        size_t dn = strlen(declaratorPart);
        size_t start = 0, k;
        for (k = 0; k <= dn; ++k) {
            if (k == dn || declaratorPart[k] == ',') {
                if (k > start) {
                    char *partRaw = aide_strndup(declaratorPart + start, k - start);
                    char *cursor = aide_str_trim_dup(partRaw);
                    AideStr type;
                    free(partRaw);

                    aide_str_init(&type);
                    aide_str_append(&type, baseType.data);
                    while (cursor[0] == '*') {
                        char *rest = aide_str_trim_dup(cursor + 1);
                        aide_str_append_char(&type, '*');
                        free(cursor);
                        cursor = rest;
                    }
                    if (cursor[0] != '\0')
                        pendinglist_push(members, type.data, cursor, lineNumber);
                    aide_str_free(&type);
                    free(cursor);
                }
                start = k + 1;
            }
        }
    }

    free(declaratorPart);
    aide_str_free(&baseType);
    free(decl);
    free(trimmed);
}

/* Computes offsets for `pendingMembers`, emits one StructMember Symbol
 * per field, backfills the owning Struct/Union/Typedef symbol's total
 * size, and remembers that size in `localStructSizes` -- see the Qt
 * counterpart for the exact same-file-only embedding rule this mirrors. */
static void finalize_struct_members(AideCParseResult *result, const char *structName, AideSymbolKind structKind,
                                     const PendingMemberList *pendingMembers, AideStrIntMap *localStructSizes,
                                     const char *absoluteFilePath)
{
    AideMemberDecl *decls;
    AideStructLayoutResult layout;
    size_t i;

    if (structName[0] == '\0')
        return;

    decls = (AideMemberDecl *)malloc((pendingMembers->count > 0 ? pendingMembers->count : 1) * sizeof(AideMemberDecl));
    for (i = 0; i < pendingMembers->count; ++i) {
        decls[i].type = pendingMembers->items[i].type;
        decls[i].name = pendingMembers->items[i].name;
    }

    aide_structlayout_result_init(&layout);
    aide_structlayout_compute(decls, pendingMembers->count, localStructSizes, &layout);
    free(decls);

    for (i = 0; i < layout.memberCount && i < pendingMembers->count; ++i) {
        AideSymbol *s = aide_symbollist_push_new(&result->symbols);
        free(s->name); s->name = aide_strdup(layout.members[i].name);
        s->kind = AIDE_SYM_STRUCT_MEMBER;
        free(s->file); s->file = aide_strdup(absoluteFilePath);
        s->line = pendingMembers->items[i].line;
        free(s->signature); s->signature = aide_strdup(layout.members[i].type);
        free(s->scope); s->scope = aide_strdup(structName);
        s->offset = layout.members[i].offset;
    }

    for (i = 0; i < result->symbols.count; ++i) {
        AideSymbol *s = &result->symbols.items[i];
        if (strcmp(s->name, structName) == 0 && strcmp(s->file, absoluteFilePath) == 0
            && (s->kind == structKind || s->kind == AIDE_SYM_TYPEDEF)) {
            s->offset = layout.totalSize;
        }
    }

    aide_strintmap_set(localStructSizes, structName, layout.totalSize);
    aide_structlayout_result_free(&layout);
}

/* ---- top-level parse() ---- */

static AideRegex *s_defineRe = NULL;
static AideRegex *s_includeRe = NULL;
static AideRegex *s_typedefSimpleRe = NULL;
static AideRegex *s_typedefBlockStartRe = NULL; /* without the trailing \b -- checked manually */
static AideRegex *s_typedefBlockEndRe = NULL;
static AideRegex *s_taggedStructRe = NULL;
static AideRegex *s_controlKeywordRe = NULL; /* without the trailing \b -- checked manually */
static AideRegex *s_bareClosingBraceRe = NULL;

static void ensure_compiled(void)
{
    if (!s_defineRe)
        s_defineRe = aide_regex_compile("^[[:space:]]*#[[:space:]]*define[[:space:]]+([A-Za-z0-9_]+)", 0);
    if (!s_includeRe)
        s_includeRe = aide_regex_compile("^[[:space:]]*#[[:space:]]*include[[:space:]]*([<\"])([^\">]+)[\">]", 0);
    if (!s_typedefSimpleRe)
        s_typedefSimpleRe = aide_regex_compile(
            "^[[:space:]]*typedef[[:space:]]+[^{}();]+[[:space:]*]([A-Za-z0-9_]+)[[:space:]]*;", 0);
    if (!s_typedefBlockStartRe)
        s_typedefBlockStartRe = aide_regex_compile("^[[:space:]]*typedef[[:space:]]+(struct|union|enum)", 0);
    if (!s_typedefBlockEndRe)
        s_typedefBlockEndRe = aide_regex_compile("^[[:space:]]*\\}[[:space:]]*([A-Za-z0-9_]+)[[:space:]]*;", 0);
    if (!s_taggedStructRe)
        s_taggedStructRe = aide_regex_compile(
            "^[[:space:]]*(struct|union|enum)[[:space:]]+([A-Za-z0-9_]+)[[:space:]]*\\{", 0);
    if (!s_controlKeywordRe)
        s_controlKeywordRe = aide_regex_compile("^[[:space:]]*(if|for|while|switch|return|sizeof|else)", 0);
    if (!s_bareClosingBraceRe)
        s_bareClosingBraceRe = aide_regex_compile("^[[:space:]]*\\}[[:space:]]*;?[[:space:]]*$", 0);
}

/* Emulates a trailing \b after a regex match ending at byte offset `pos`
 * in `line` (POSIX ERE has no \b). Holds iff end-of-string, or the next
 * character isn't a word character. */
static int boundary_after(const char *line, long pos)
{
    return line[pos] == '\0' || !is_word_c(line[pos]);
}

void aide_cparser_parse(const char *text, const char *absoluteFilePath, AideCParseResult *out)
{
    LineRefList rawLines;
    AideStrList lines;
    int braceDepth = 0;
    int inTypedefBlock = 0;
    int collectingMembers = 0;
    AideSymbolKind structKindForMembers = AIDE_SYM_STRUCT;
    AideStr structNameForMembers;
    PendingMemberList pendingMembers;
    AideStrIntMap localStructSizes;
    size_t lineIndex;

    ensure_compiled();
    aide_cparser_result_init(out);

    split_lines_ref(text, &rawLines);
    strip_comments_and_strings(&rawLines, &lines);

    aide_str_init(&structNameForMembers);
    pendinglist_init(&pendingMembers);
    aide_strintmap_init(&localStructSizes);

    for (lineIndex = 0; lineIndex < lines.count; ++lineIndex) {
        const char *line = lines.items[lineIndex];
        const int lineNumber = (int)lineIndex + 1;
        AideRegexMatch m;

        /* Matched against the raw (unstripped) line: the comment/string
         * stripping pass treats "..." as a string literal and blanks its
         * contents, which would otherwise eat quoted include paths. */
        {
            char *rawLine = aide_strndup(rawLines.items[lineIndex].start, rawLines.items[lineIndex].len);
            if (aide_regex_exec(s_includeRe, rawLine, 0, &m)) {
                char *path = aide_strndup(rawLine + m.start[2], (size_t)(m.end[2] - m.start[2]));
                int quoted = rawLine[m.start[1]] == '"';
                includelist_push(&out->includes, path, quoted);
                free(path);
            }
            free(rawLine);
        }

        if (braceDepth == 0) {
            if (aide_regex_exec(s_defineRe, line, 0, &m)) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_DEFINE;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
            } else if (aide_regex_exec(s_typedefBlockStartRe, line, 0, &m) && boundary_after(line, m.end[1])) {
                char *tag = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                inTypedefBlock = 1;
                if (strcmp(tag, "struct") == 0 || strcmp(tag, "union") == 0) {
                    collectingMembers = 1;
                    structKindForMembers = strcmp(tag, "struct") == 0 ? AIDE_SYM_STRUCT : AIDE_SYM_UNION;
                    aide_str_clear(&structNameForMembers);
                    pendinglist_clear(&pendingMembers);
                }
                free(tag);
            } else if (aide_regex_exec(s_typedefSimpleRe, line, 0, &m)) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_TYPEDEF;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
            } else if (aide_regex_exec(s_taggedStructRe, line, 0, &m)) {
                char *tag = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[2], (size_t)(m.end[2] - m.start[2]));
                s->kind = strcmp(tag, "struct") == 0 ? AIDE_SYM_STRUCT
                        : strcmp(tag, "union") == 0 ? AIDE_SYM_UNION : AIDE_SYM_ENUM;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;

                if (s->kind == AIDE_SYM_STRUCT || s->kind == AIDE_SYM_UNION) {
                    collectingMembers = 1;
                    structKindForMembers = s->kind;
                    aide_str_clear(&structNameForMembers);
                    aide_str_append(&structNameForMembers, s->name);
                    pendinglist_clear(&pendingMembers);
                }
                free(tag);
            } else if (!(aide_regex_exec(s_controlKeywordRe, line, 0, &m) && boundary_after(line, m.end[1]))) {
                char *name, *params;
                int hasBrace;
                if (try_match_function_decl(line, &name, &params, &hasBrace)) {
                    AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                    free(s->name); s->name = name;
                    s->kind = hasBrace ? AIDE_SYM_FUNCTION : AIDE_SYM_PROTOTYPE;
                    free(s->file); s->file = aide_strdup(absoluteFilePath);
                    s->line = lineNumber;
                    free(s->signature); s->signature = aide_str_trim_dup(line);
                    free(params);
                }
            }
        } else if (braceDepth == 1 && inTypedefBlock) {
            if (aide_regex_exec(s_typedefBlockEndRe, line, 0, &m)) {
                AideSymbol *s = aide_symbollist_push_new(&out->symbols);
                free(s->name); s->name = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                s->kind = AIDE_SYM_TYPEDEF;
                free(s->file); s->file = aide_strdup(absoluteFilePath);
                s->line = lineNumber;
                inTypedefBlock = 0;

                if (collectingMembers) {
                    aide_str_clear(&structNameForMembers);
                    aide_str_append(&structNameForMembers, s->name);
                    finalize_struct_members(out, structNameForMembers.data, structKindForMembers, &pendingMembers,
                                             &localStructSizes, absoluteFilePath);
                    collectingMembers = 0;
                }
            } else if (collectingMembers) {
                parse_member_decl_line(line, lineNumber, &pendingMembers);
            }
        } else if (braceDepth == 1 && collectingMembers) {
            if (aide_regex_exec(s_bareClosingBraceRe, line, 0, &m)) {
                finalize_struct_members(out, structNameForMembers.data, structKindForMembers, &pendingMembers,
                                         &localStructSizes, absoluteFilePath);
                collectingMembers = 0;
            } else {
                parse_member_decl_line(line, lineNumber, &pendingMembers);
            }
        }

        {
            size_t ci;
            size_t lineLen = strlen(line);
            for (ci = 0; ci < lineLen; ++ci) {
                if (line[ci] == '{')
                    ++braceDepth;
                else if (line[ci] == '}')
                    braceDepth = braceDepth > 0 ? braceDepth - 1 : 0;
            }
        }
        if (braceDepth == 0) {
            inTypedefBlock = 0;
            collectingMembers = 0; /* safety net for malformed/unbalanced input */
        }
    }

    aide_strintmap_free(&localStructSizes);
    pendinglist_free(&pendingMembers);
    aide_str_free(&structNameForMembers);
    aide_strlist_free(&lines);
    free(rawLines.items);
}
