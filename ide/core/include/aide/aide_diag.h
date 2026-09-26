#ifndef AIDE_DIAG_H
#define AIDE_DIAG_H

/* Parses vc/vbccm68k, vasmm68k_mot and vlink diagnostics out of build
 * output. Direct C99 port of the Qt DiagnosticsParser -- see that
 * header (src/build/DiagnosticsParser.h) for the exact formats this
 * recognizes and the verified toolchain quirks (vc uses the same
 * "warning N in line L of "file": msg" format as vasm, not a
 * "file:line: warning" one-liner). */

#include "aide/aide_issue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses a single line (NUL-terminated, no embedded newline). On a
 * match, fills *out (caller owns out->file/out->message and must
 * aide_issue_release() them) and returns 1. Returns 0 for non-matching
 * lines (context lines starting with '>', summary lines, ...) and
 * leaves *out untouched. */
int aide_diag_parse_line(const char *line, AideIssue *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_DIAG_H */
