#ifndef AIDE_STR_H
#define AIDE_STR_H

/* Minimal dynamic string / string-list utilities shared by all core/
 * modules. No Qt, no libc beyond stdlib/string/stdio -- must compile
 * with vbcc -c99 on m68k as well as any host C99 compiler. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideStr {
    char *data;   /* NUL-terminated, always valid (points at a static
                     empty string when len==0 and no buffer is owned) */
    size_t len;
    size_t cap;   /* 0 while data points at the static empty string */
} AideStr;

void aide_str_init(AideStr *s);
void aide_str_free(AideStr *s);
void aide_str_clear(AideStr *s);
void aide_str_reserve(AideStr *s, size_t min_cap);
void aide_str_append_n(AideStr *s, const char *text, size_t n);
void aide_str_append(AideStr *s, const char *text);
void aide_str_append_char(AideStr *s, char c);
/* Appends `suffixWithLeadingSlash` (always literally "/...") to `s`,
 * skipping the redundant leading '/' if `s` already ends in a path
 * separator. A POSIX path never does, but an AmigaDOS assign root
 * ("vbcc:") does via its trailing ':' -- and "vbcc:" + "/bin/vc" is a
 * path AmigaDOS itself rejects (the correct form is "vbcc:bin/vc").
 * Used everywhere a toolchain/SDK root (which may be either kind of
 * root depending on host vs. Amiga) gets a fixed subpath appended. */
void aide_str_append_path(AideStr *s, const char *suffixWithLeadingSlash);
void aide_str_appendf(AideStr *s, const char *fmt, ...);
/* Transfers ownership of the heap buffer to the caller (malloc'd, the
 * caller must free() it) and resets *s to empty. Never returns NULL. */
char *aide_str_take(AideStr *s);

typedef struct AideStrList {
    char **items; /* each entry malloc'd, NUL-terminated */
    size_t count;
    size_t cap;
} AideStrList;

void aide_strlist_init(AideStrList *l);
void aide_strlist_free(AideStrList *l);
void aide_strlist_clear(AideStrList *l);
void aide_strlist_push(AideStrList *l, const char *s);      /* copies s */
void aide_strlist_push_owned(AideStrList *l, char *s);       /* takes ownership */
void aide_strlist_push_n(AideStrList *l, const char *s, size_t n);

/* Duplicates [text, text+n) into a fresh malloc'd, NUL-terminated buffer. */
char *aide_strndup(const char *text, size_t n);
char *aide_strdup(const char *text);

/* Mirrors QString::trimmed(): strips leading/trailing ASCII whitespace
 * (space, \t, \n, \v, \f, \r), returns a fresh malloc'd copy. */
char *aide_str_trim_dup(const char *text);

/* Case-insensitive ASCII string equality (a hand-rolled strcasecmp==0:
 * strcasecmp/stricmp are BSD/POSIX/MSVC extensions, not standard C99, and
 * not guaranteed present in vbcc's vclib -- see the module comment above). */
int aide_str_ieq(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_STR_H */
