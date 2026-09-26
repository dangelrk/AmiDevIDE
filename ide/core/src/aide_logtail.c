#include "aide/aide_logtail.h"

#include <stdlib.h>

void aide_logtail_init(AideLogTail *t)
{
    aide_str_init(&t->partial);
}

void aide_logtail_free(AideLogTail *t)
{
    aide_str_free(&t->partial);
}

void aide_logtail_feed(AideLogTail *t, const char *chunk, size_t len,
                        AideLogTailLineFn cb, void *userdata)
{
    size_t lineStart;
    size_t i;

    aide_str_append_n(&t->partial, chunk, len);

    /* Re-scan from the start each time rather than tracking an index
     * across calls -- feed() is called at most once per ~250ms timer
     * tick (see mainwindow.c's build_tick()), so partial.len is at most
     * a few source files' worth of compiler output, never large enough
     * for an O(n) rescan to matter. */
    lineStart = 0;
    for (i = 0; i < t->partial.len; ++i) {
        size_t lineEnd;
        char *line;

        if (t->partial.data[i] != '\n')
            continue;

        lineEnd = i;
        if (lineEnd > lineStart && t->partial.data[lineEnd - 1] == '\r')
            --lineEnd; /* CRLF -- drop the '\r' too */

        line = aide_strndup(t->partial.data + lineStart, lineEnd - lineStart);
        cb(line, userdata);
        free(line);

        lineStart = i + 1;
    }

    if (lineStart > 0) {
        /* Drop the consumed prefix, keep whatever's left (the start of
         * the next, still-incomplete line) for the next feed(). */
        size_t remaining = t->partial.len - lineStart;
        AideStr next;
        aide_str_init(&next);
        aide_str_append_n(&next, t->partial.data + lineStart, remaining);
        aide_str_free(&t->partial);
        t->partial = next;
    }
}

void aide_logtail_flush(AideLogTail *t, AideLogTailLineFn cb, void *userdata)
{
    if (t->partial.len > 0)
        cb(t->partial.data, userdata);
    aide_str_clear(&t->partial);
}
