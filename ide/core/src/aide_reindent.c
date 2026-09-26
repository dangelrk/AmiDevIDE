#include "aide/aide_reindent.h"
#include "aide/aide_str.h"

#include <string.h>

char *aide_reindent_braces(const char *text, const char *indentUnit)
{
    AideStr out;
    size_t i = 0, n = strlen(text);
    int depth = 0;
    int inBlockComment = 0;

    aide_str_init(&out);

    while (i < n) {
        size_t lineStart = i;
        size_t lineEnd = lineStart;
        size_t contentStart;
        int wasInBlockComment = inBlockComment;
        int inString = 0, inChar = 0;
        int lineDelta = 0;
        int firstIsCloser;
        size_t j;

        while (lineEnd < n && text[lineEnd] != '\n')
            lineEnd++;

        contentStart = lineStart;
        while (contentStart < lineEnd && (text[contentStart] == ' ' || text[contentStart] == '\t'))
            contentStart++;

        firstIsCloser = (!wasInBlockComment && contentStart < lineEnd && text[contentStart] == '}');

        for (j = lineStart; j < lineEnd; ++j) {
            char c = text[j];
            char c2 = (j + 1 < lineEnd) ? text[j + 1] : '\0';

            if (inBlockComment) {
                if (c == '*' && c2 == '/') { inBlockComment = 0; ++j; }
                continue;
            }
            if (inString) {
                if (c == '\\') { ++j; continue; }
                if (c == '"') inString = 0;
                continue;
            }
            if (inChar) {
                if (c == '\\') { ++j; continue; }
                if (c == '\'') inChar = 0;
                continue;
            }
            if (c == '/' && c2 == '*') { inBlockComment = 1; ++j; continue; }
            if (c == '/' && c2 == '/') break; /* line comment -- rest of line ignored */
            if (c == '"') { inString = 1; continue; }
            if (c == '\'') { inChar = 1; continue; }
            if (c == '{') ++lineDelta;
            else if (c == '}') --lineDelta;
        }

        if (wasInBlockComment) {
            /* Continuation of a multi-line comment -- left byte-for-byte. */
            aide_str_append_n(&out, text + lineStart, lineEnd - lineStart);
        } else if (contentStart < lineEnd) {
            int lineLevel = depth - (firstIsCloser ? 1 : 0);
            size_t k;
            if (lineLevel < 0) lineLevel = 0;
            for (k = 0; k < (size_t)lineLevel; ++k)
                aide_str_append(&out, indentUnit);
            aide_str_append_n(&out, text + contentStart, lineEnd - contentStart);
        }
        /* blank lines stay blank -- no indentation added to nothing */

        depth += lineDelta;
        if (depth < 0) depth = 0;

        if (lineEnd < n) {
            aide_str_append_char(&out, '\n');
            i = lineEnd + 1;
        } else {
            i = lineEnd;
        }
    }

    return aide_str_take(&out);
}
