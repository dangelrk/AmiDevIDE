#include "aide/aide_str.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_empty[1] = { 0 };

void aide_str_init(AideStr *s)
{
    s->data = s_empty;
    s->len = 0;
    s->cap = 0;
}

void aide_str_free(AideStr *s)
{
    if (s->cap > 0)
        free(s->data);
    aide_str_init(s);
}

void aide_str_clear(AideStr *s)
{
    if (s->cap > 0)
        s->data[0] = '\0';
    s->len = 0;
}

void aide_str_reserve(AideStr *s, size_t min_cap)
{
    size_t new_cap;
    char *new_data;

    if (s->cap >= min_cap)
        return;

    new_cap = s->cap > 0 ? s->cap : 16;
    while (new_cap < min_cap)
        new_cap *= 2;

    new_data = (char *)malloc(new_cap);
    memcpy(new_data, s->data, s->len + 1); /* include NUL */
    if (s->cap > 0)
        free(s->data);
    s->data = new_data;
    s->cap = new_cap;
}

void aide_str_append_n(AideStr *s, const char *text, size_t n)
{
    if (n == 0)
        return;
    aide_str_reserve(s, s->len + n + 1);
    memcpy(s->data + s->len, text, n);
    s->len += n;
    s->data[s->len] = '\0';
}

void aide_str_append(AideStr *s, const char *text)
{
    aide_str_append_n(s, text, strlen(text));
}

void aide_str_append_char(AideStr *s, char c)
{
    aide_str_append_n(s, &c, 1);
}

void aide_str_append_path(AideStr *s, const char *suffixWithLeadingSlash)
{
    if (s->len > 0 && (s->data[s->len - 1] == '/' || s->data[s->len - 1] == ':'))
        suffixWithLeadingSlash++;
    aide_str_append(s, suffixWithLeadingSlash);
}

void aide_str_appendf(AideStr *s, const char *fmt, ...)
{
    va_list ap;
    int needed;
    char stackbuf[256];

    va_start(ap, fmt);
    needed = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);

    if (needed < 0)
        return;

    if ((size_t)needed < sizeof(stackbuf)) {
        aide_str_append_n(s, stackbuf, (size_t)needed);
        return;
    }

    {
        char *heapbuf = (char *)malloc((size_t)needed + 1);
        va_start(ap, fmt);
        vsnprintf(heapbuf, (size_t)needed + 1, fmt, ap);
        va_end(ap);
        aide_str_append_n(s, heapbuf, (size_t)needed);
        free(heapbuf);
    }
}

char *aide_str_take(AideStr *s)
{
    char *result;
    if (s->cap == 0) {
        result = (char *)malloc(1);
        result[0] = '\0';
    } else {
        result = s->data;
    }
    aide_str_init(s);
    return result;
}

char *aide_strndup(const char *text, size_t n)
{
    char *out = (char *)malloc(n + 1);
    memcpy(out, text, n);
    out[n] = '\0';
    return out;
}

char *aide_strdup(const char *text)
{
    return aide_strndup(text, strlen(text));
}

static int is_ascii_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

char *aide_str_trim_dup(const char *text)
{
    size_t start = 0;
    size_t end = strlen(text);
    while (start < end && is_ascii_space_char(text[start]))
        ++start;
    while (end > start && is_ascii_space_char(text[end - 1]))
        --end;
    return aide_strndup(text + start, end - start);
}

static char to_ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int aide_str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (to_ascii_lower(*a) != to_ascii_lower(*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* ---- AideStrList ---- */

void aide_strlist_init(AideStrList *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void aide_strlist_clear(AideStrList *l)
{
    size_t i;
    for (i = 0; i < l->count; ++i)
        free(l->items[i]);
    l->count = 0;
}

void aide_strlist_free(AideStrList *l)
{
    aide_strlist_clear(l);
    free(l->items);
    l->items = NULL;
    l->cap = 0;
}

static void strlist_reserve(AideStrList *l, size_t min_cap)
{
    size_t new_cap;
    char **new_items;

    if (l->cap >= min_cap)
        return;

    new_cap = l->cap > 0 ? l->cap : 8;
    while (new_cap < min_cap)
        new_cap *= 2;

    new_items = (char **)malloc(new_cap * sizeof(char *));
    memcpy(new_items, l->items, l->count * sizeof(char *));
    free(l->items);
    l->items = new_items;
    l->cap = new_cap;
}

void aide_strlist_push_owned(AideStrList *l, char *s)
{
    strlist_reserve(l, l->count + 1);
    l->items[l->count++] = s;
}

void aide_strlist_push(AideStrList *l, const char *s)
{
    aide_strlist_push_owned(l, aide_strdup(s));
}

void aide_strlist_push_n(AideStrList *l, const char *s, size_t n)
{
    aide_strlist_push_owned(l, aide_strndup(s, n));
}

