/* Host backend for aide_regex.h on Windows, which has no POSIX <regex.h>:
 * std::regex with the POSIX ERE grammar (std::regex::extended), so the
 * patterns mean the same as with aide_regex_posix.c on macOS/Linux.
 * Selected by core/CMakeLists.txt for WIN32 in place of
 * aide_regex_posix.c. */

#include "aide/aide_regex.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <regex>

struct AideRegex {
    std::regex re;
};

extern "C" AideRegex *aide_regex_compile(const char *pattern, int flags)
{
    std::regex::flag_type f = std::regex::extended;
    char *expanded = aide_regex_expand_shorthand(pattern);
    AideRegex *r = nullptr;

    if (flags & AIDE_REGEX_ICASE)
        f |= std::regex::icase;
    if (flags & AIDE_REGEX_NOSUB)
        f |= std::regex::nosubs;

    try {
        r = new AideRegex{std::regex(expanded, f)};
    } catch (...) {   /* std::regex_error on a bad pattern, bad_alloc */
        r = nullptr;
    }
    std::free(expanded);
    return r;
}

extern "C" void aide_regex_free(AideRegex *re)
{
    delete re;
}

extern "C" int aide_regex_exec(const AideRegex *re, const char *text, size_t from,
                                AideRegexMatch *match)
{
    const char *begin = text + from;
    const char *end = begin + std::strlen(begin);
    std::cmatch m;
    int i;

    try {
        if (!std::regex_search(begin, end, m, re->re))
            return 0;
    } catch (...) {   /* error_complexity / error_stack on huge input */
        return 0;
    }

    match->group_count = 0;
    for (i = 0; i < AIDE_REGEX_MAX_GROUPS; ++i) {
        if (static_cast<size_t>(i) >= m.size() || !m[i].matched) {
            match->start[i] = -1;
            match->end[i] = -1;
            continue;
        }
        match->start[i] = (long)from + (long)m.position(i);
        match->end[i] = (long)from + (long)(m.position(i) + m.length(i));
        match->group_count = i + 1;
    }
    return 1;
}

extern "C" int aide_regex_matches(const AideRegex *re, const char *text)
{
    AideRegexMatch m;
    return aide_regex_exec(re, text, 0, &m);
}
