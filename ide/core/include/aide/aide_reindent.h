#ifndef AIDE_REINDENT_H
#define AIDE_REINDENT_H

/* Simple brace-depth reindenter for the "Format"/"Reindent" editor
 * action (both UIs -- see qt/editor/EditorWidget.cpp's live indent-on-
 * Enter for the complementary "as you type" behavior, which this does
 * NOT attempt to replace; TextEditor.mcc on the Amiga side has no hook
 * for intercepting Enter, so this batch action is that side's only
 * option, per user feedback).
 *
 * Re-derives each line's leading whitespace from a running brace-depth
 * count, replacing whatever leading whitespace it previously had.
 * Skips real string/char literals and // and slash-star block comments
 * when counting braces (a literal brace inside one of those must not
 * affect the count), and leaves the body lines of a multi-line block
 * comment completely untouched (byte-for-byte) rather than presuming to
 * reindent arbitrary comment text (ASCII art, aligned columns, etc).
 * Not a real parser/formatter -- assembler files have no braces at all,
 * so running this on one is a harmless no-op. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a fresh malloc'd, reindented copy of `text` (indentUnit, e.g.
 * "    " or "\t", repeated per brace-depth level). Never returns NULL. */
char *aide_reindent_braces(const char *text, const char *indentUnit);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_REINDENT_H */
