#include "aide/aide_sfd.h"
#include "aide/aide_fs.h"
#include "aide/aide_regex.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_space_c(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static int is_word_c(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

void aide_sfdindex_init(AideSfdIndex *idx)
{
    idx->items = NULL;
    idx->count = 0;
    idx->cap = 0;
}

static void free_function(AideSfdFunction *fn)
{
    free(fn->name);
    free(fn->library);
    free(fn->rawSignature);
    free(fn->registers);
}

static void clear_index(AideSfdIndex *idx)
{
    size_t i;
    for (i = 0; i < idx->count; ++i)
        free_function(&idx->items[i]);
    idx->count = 0;
}

void aide_sfdindex_free(AideSfdIndex *idx)
{
    clear_index(idx);
    free(idx->items);
    idx->items = NULL;
    idx->cap = 0;
}

static void set_function(AideSfdIndex *idx, const char *name, const char *library,
                          const char *rawSignature, const char *registers)
{
    size_t i;
    for (i = 0; i < idx->count; ++i) {
        if (strcmp(idx->items[i].name, name) == 0) {
            free_function(&idx->items[i]);
            idx->items[i].name = aide_strdup(name);
            idx->items[i].library = aide_strdup(library);
            idx->items[i].rawSignature = aide_strdup(rawSignature);
            idx->items[i].registers = aide_strdup(registers);
            return;
        }
    }
    if (idx->count >= idx->cap) {
        size_t newCap = idx->cap > 0 ? idx->cap * 2 : 64;
        idx->items = (AideSfdFunction *)realloc(idx->items, newCap * sizeof(AideSfdFunction));
        idx->cap = newCap;
    }
    idx->items[idx->count].name = aide_strdup(name);
    idx->items[idx->count].library = aide_strdup(library);
    idx->items[idx->count].rawSignature = aide_strdup(rawSignature);
    idx->items[idx->count].registers = aide_strdup(registers);
    idx->count++;
}

const AideSfdFunction *aide_sfdindex_find(const AideSfdIndex *idx, const char *functionName)
{
    size_t i;
    for (i = 0; i < idx->count; ++i) {
        if (strcmp(idx->items[i].name, functionName) == 0)
            return &idx->items[i];
    }
    return NULL;
}

/* Splits a joined "ReturnType Name(params) (regs)" entry, handling
 * nested parens in params (e.g. function-pointer arguments). Returns 1
 * if the entry looks like a valid SFD function line; all three outputs
 * are malloc'd on success. */
static int split_entry(const char *entry, char **outReturnAndName, char **outParams, char **outRegisters)
{
    char *trimmed = aide_str_trim_dup(entry);
    size_t len = strlen(trimmed);
    long regOpen;
    long i;
    size_t beforeRegsLen;
    char *beforeRegs;
    int depth;
    long paramsOpen;

    if (len == 0 || trimmed[len - 1] != ')') {
        free(trimmed);
        return 0;
    }

    regOpen = -1;
    for (i = (long)len - 1; i >= 0; --i) {
        if (trimmed[i] == '(') { regOpen = i; break; }
    }
    if (regOpen < 0) {
        free(trimmed);
        return 0;
    }
    *outRegisters = aide_strndup(trimmed + regOpen + 1, len - (size_t)regOpen - 2);

    {
        char *beforeRegsRaw = aide_strndup(trimmed, (size_t)regOpen);
        beforeRegs = aide_str_trim_dup(beforeRegsRaw);
        free(beforeRegsRaw);
    }
    beforeRegsLen = strlen(beforeRegs);
    if (beforeRegsLen == 0 || beforeRegs[beforeRegsLen - 1] != ')') {
        free(*outRegisters);
        free(beforeRegs);
        free(trimmed);
        return 0;
    }

    depth = 0;
    paramsOpen = -1;
    for (i = (long)beforeRegsLen - 1; i >= 0; --i) {
        if (beforeRegs[i] == ')') {
            ++depth;
        } else if (beforeRegs[i] == '(') {
            --depth;
            if (depth == 0) { paramsOpen = i; break; }
        }
    }
    if (paramsOpen < 0) {
        free(*outRegisters);
        free(beforeRegs);
        free(trimmed);
        return 0;
    }

    *outParams = aide_strndup(beforeRegs + paramsOpen + 1, beforeRegsLen - (size_t)paramsOpen - 2);
    {
        char *retNameRaw = aide_strndup(beforeRegs, (size_t)paramsOpen);
        *outReturnAndName = aide_str_trim_dup(retNameRaw);
        free(retNameRaw);
    }

    free(beforeRegs);
    free(trimmed);
    return 1;
}

/* Mirrors the unanchored Qt pattern `(\w+)\s*$`: the trailing run of
 * word characters at the end of the string (after trailing whitespace),
 * or NULL if the string doesn't end in one (see the module's design
 * notes for why this hand scan is exactly equivalent). */
static char *extract_trailing_identifier(const char *text)
{
    size_t len = strlen(text);
    size_t end = len;
    size_t start;

    while (end > 0 && is_space_c(text[end - 1]))
        --end;
    start = end;
    while (start > 0 && is_word_c(text[start - 1]))
        --start;
    if (start == end)
        return NULL;
    return aide_strndup(text + start, end - start);
}

static AideRegex *s_libnameRe = NULL;

static void ensure_compiled(void)
{
    if (!s_libnameRe)
        s_libnameRe = aide_regex_compile("^==libname[[:space:]]+([^[:space:]]+)", 0);
}

static void finalize_entry(AideSfdIndex *idx, const char *accumulator, const char *libname)
{
    char *returnAndName, *params, *registers;
    if (accumulator[0] == '\0')
        return;
    if (split_entry(accumulator, &returnAndName, &params, &registers)) {
        char *name = extract_trailing_identifier(returnAndName);
        if (name) {
            char *rawSignature;
            {
                /* .simplified(): collapse any run of whitespace to a
                 * single space and trim both ends -- the continuation
                 * lines were already joined with single spaces while
                 * accumulating, so this only needs to also collapse any
                 * *internal* multi-space/tab runs from the source text. */
                AideStr s;
                size_t i, n = strlen(accumulator);
                int lastWasSpace = 1;
                aide_str_init(&s);
                for (i = 0; i < n; ++i) {
                    if (is_space_c(accumulator[i])) {
                        if (!lastWasSpace)
                            aide_str_append_char(&s, ' ');
                        lastWasSpace = 1;
                    } else {
                        aide_str_append_char(&s, accumulator[i]);
                        lastWasSpace = 0;
                    }
                }
                while (s.len > 0 && s.data[s.len - 1] == ' ')
                    s.data[--s.len] = '\0';
                rawSignature = aide_str_take(&s);
            }
            set_function(idx, name, libname, rawSignature, registers);
            free(rawSignature);
            free(name);
        }
        free(returnAndName);
        free(params);
        free(registers);
    }
}

static void parse_file(AideSfdIndex *idx, const char *path)
{
    char *data;
    size_t size;
    char *libname;
    AideStr accumulator;
    size_t lineStart, i, n;

    ensure_compiled();

    if (!aide_fs_read_file(path, &data, &size))
        return;

    libname = aide_strdup("");
    aide_str_init(&accumulator);

    n = size;
    lineStart = 0;
    for (i = 0; i <= n; ++i) {
        if (i != n && data[i] != '\n')
            continue;

        {
            size_t rawLen = i - lineStart;
            /* Lines may be CRLF-terminated in real SFD files; strip a
             * trailing \r, matching QTextStream::readLineInto's
             * cross-platform line-ending handling. */
            if (rawLen > 0 && data[lineStart + rawLen - 1] == '\r')
                --rawLen;

            char *line = aide_strndup(data + lineStart, rawLen);
            char *trimmedLine = aide_str_trim_dup(line);

            if (strncmp(line, "==", 2) == 0) {
                finalize_entry(idx, accumulator.data, libname);
                aide_str_clear(&accumulator);

                {
                    AideRegexMatch m;
                    if (aide_regex_exec(s_libnameRe, line, 0, &m)) {
                        free(libname);
                        libname = aide_strndup(line + m.start[1], (size_t)(m.end[1] - m.start[1]));
                    }
                }
            } else if (trimmedLine[0] == '\0') {
                /* blank line: ignored */
            } else if (line[0] == '\t' || line[0] == ' ') {
                aide_str_append_char(&accumulator, ' ');
                aide_str_append(&accumulator, trimmedLine);
            } else {
                finalize_entry(idx, accumulator.data, libname);
                aide_str_clear(&accumulator);
                aide_str_append(&accumulator, line);
            }

            free(line);
            free(trimmedLine);
        }

        lineStart = i + 1;
    }
    finalize_entry(idx, accumulator.data, libname);

    aide_str_free(&accumulator);
    free(libname);
    free(data);
}

void aide_sfdindex_reload(AideSfdIndex *idx, const AideStrList *sfdDirs)
{
    size_t i;
    clear_index(idx);

    for (i = 0; i < sfdDirs->count; ++i) {
        AideStrList files;
        size_t j;
        aide_strlist_init(&files);
        aide_fs_list_files(sfdDirs->items[i], ".sfd", &files);
        for (j = 0; j < files.count; ++j) {
            char *fullPath = aide_fs_join(sfdDirs->items[i], files.items[j]);
            parse_file(idx, fullPath);
            free(fullPath);
        }
        aide_strlist_free(&files);
    }
}
