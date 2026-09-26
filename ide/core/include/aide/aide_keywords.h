#ifndef AIDE_KEYWORDS_H
#define AIDE_KEYWORDS_H

/* Portable C99 mirror of editor/CompletionModel.h -- the static keyword
 * list (C keywords, Amiga C types, m68k directives/mnemonics) that seeds
 * completion alongside the live symbol index and SFD function names. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the static keyword table; *outCount is its length. The
 * returned pointer is static -- never free() it or its entries. */
const char *const *aide_keywords_static(size_t *outCount);

/* ACE BASIC vocabulary, generated from the ACE repo's CubicIDE
 * dictionaries by core/tools/gen_basic_keywords.sh. Commands and functions
 * are kept apart so a highlighter can colour them differently, the way
 * CubicIDE does. Both tables are static -- never free them. */
const char *const *aide_keywords_basic_commands(size_t *outCount);
const char *const *aide_keywords_basic_functions(size_t *outCount);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_KEYWORDS_H */
