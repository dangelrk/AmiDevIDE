#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <string.h>

char *aide_regex_escape(const char *text)
{
    AideStr s;
    const char *p;
    aide_str_init(&s);

    for (p = text; *p; ++p) {
        if (strchr(".^$*+?()[]{}|\\", *p) != NULL)
            aide_str_append_char(&s, '\\');
        aide_str_append_char(&s, *p);
    }
    return aide_str_take(&s);
}

char *aide_regex_wildcard_to_ere(const char *wildcard)
{
    AideStr s;
    const char *p;
    aide_str_init(&s);
    aide_str_append_char(&s, '^');

    for (p = wildcard; *p; ++p) {
        switch (*p) {
        case '*':
            aide_str_append(&s, ".*");
            break;
        case '?':
            aide_str_append_char(&s, '.');
            break;
        default:
            if (strchr(".^$+()[]{}|\\", *p) != NULL)
                aide_str_append_char(&s, '\\');
            aide_str_append_char(&s, *p);
            break;
        }
    }
    aide_str_append_char(&s, '$');
    return aide_str_take(&s);
}

char *aide_regex_expand_shorthand(const char *pattern)
{
    AideStr s;
    const char *p = pattern;
    int inBracket = 0;

    aide_str_init(&s);

    while (*p) {
        if (!inBracket && *p == '[') {
            inBracket = 1;
            aide_str_append_char(&s, *p++);
            continue;
        }
        if (inBracket && *p == ']') {
            inBracket = 0;
            aide_str_append_char(&s, *p++);
            continue;
        }

        if (!inBracket && *p == '\\' && p[1] != '\0') {
            switch (p[1]) {
            case 'w': aide_str_append(&s, "[A-Za-z0-9_]"); p += 2; continue;
            case 'W': aide_str_append(&s, "[^A-Za-z0-9_]"); p += 2; continue;
            case 'd': aide_str_append(&s, "[0-9]"); p += 2; continue;
            case 'D': aide_str_append(&s, "[^0-9]"); p += 2; continue;
            case 's': aide_str_append(&s, "[[:space:]]"); p += 2; continue;
            case 'S': aide_str_append(&s, "[^[:space:]]"); p += 2; continue;
            default:
                aide_str_append_char(&s, *p++);
                aide_str_append_char(&s, *p++);
                continue;
            }
        }

        aide_str_append_char(&s, *p++);
    }

    return aide_str_take(&s);
}
