#include "aide/aide_search.h"
#include "aide/aide_fs.h"
#include "aide/aide_regex.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void aide_search_matchlist_init(AideSearchMatchList *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void aide_search_matchlist_free(AideSearchMatchList *l)
{
    size_t i;
    for (i = 0; i < l->count; ++i) {
        free(l->items[i].file);
        free(l->items[i].lineText);
    }
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static void push_match(AideSearchMatchList *l, const char *file, int line, int column, const char *lineText)
{
    if (l->count >= l->cap) {
        size_t newCap = l->cap > 0 ? l->cap * 2 : 32;
        l->items = (AideSearchMatch *)realloc(l->items, newCap * sizeof(AideSearchMatch));
        l->cap = newCap;
    }
    l->items[l->count].file = aide_strdup(file);
    l->items[l->count].line = line;
    l->items[l->count].column = column;
    l->items[l->count].lineText = aide_strdup(lineText);
    l->items[l->count].isDefinition = 0;
    l->count++;
}

static int is_word_c(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* Finds every non-overlapping match of `re` in `line`, calling
 * push_match() for each that also satisfies `wholeWord` (checked
 * manually -- see aide_search.h). Mirrors QRegularExpression::globalMatch(). */
static void search_line(const AideRegex *re, const char *line, int wholeWord, const char *file, int lineNumber,
                         AideSearchMatchList *out)
{
    size_t lineLen = strlen(line);
    size_t from = 0;

    while (from <= lineLen) {
        AideRegexMatch m;
        long start, end;
        int ok;

        if (!aide_regex_exec(re, line, from, &m))
            break;
        start = m.start[0];
        end = m.end[0];

        ok = 1;
        if (wholeWord) {
            if (start > 0 && is_word_c(line[start - 1]))
                ok = 0;
            if (end < (long)lineLen && is_word_c(line[end]))
                ok = 0;
        }
        if (ok)
            push_match(out, file, lineNumber, (int)start, line);

        from = (size_t)(end > start ? end : start + 1);
    }
}

void aide_search_search(const AideStrList *files, const char *pattern, int caseSensitive, int wholeWord,
                         int regex, AideSearchMatchList *out)
{
    char *patternText;
    AideRegex *re;
    size_t fi;

    aide_search_matchlist_free(out);
    aide_search_matchlist_init(out);

    if (pattern[0] == '\0')
        return;

    patternText = regex ? aide_strdup(pattern) : aide_regex_escape(pattern);
    re = aide_regex_compile(patternText, caseSensitive ? 0 : AIDE_REGEX_ICASE);
    free(patternText);
    if (!re)
        return; /* e.g. malformed user-supplied regex */

    for (fi = 0; fi < files->count; ++fi) {
        char *data;
        size_t size;
        size_t lineStart, i;
        int lineNumber = 0;

        if (!aide_fs_read_file(files->items[fi], &data, &size))
            continue;

        lineStart = 0;
        for (i = 0; i <= size; ++i) {
            if (i != size && data[i] != '\n')
                continue;

            {
                size_t rawLen = i - lineStart;
                if (rawLen > 0 && data[lineStart + rawLen - 1] == '\r')
                    --rawLen;
                if (i == size && rawLen == 0 && lineStart == size) {
                    /* trailing empty chunk after the final '\n' -- QTextStream's
                     * atEnd()-driven readLine() loop never produces this
                     * extra empty line, so skip it too. */
                } else {
                    char *line = aide_strndup(data + lineStart, rawLen);
                    ++lineNumber;
                    search_line(re, line, wholeWord, files->items[fi], lineNumber, out);
                    free(line);
                }
            }

            lineStart = i + 1;
        }

        free(data);
    }

    aide_regex_free(re);
}
