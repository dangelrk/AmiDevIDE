#ifndef AIDE_LOGTAIL_H
#define AIDE_LOGTAIL_H

/* Splits a stream of arbitrarily-chunked bytes (as delivered by repeated
 * aide_fs_read_range() calls against a growing build log, see
 * mainwindow.c's build_tick()) into complete lines, holding back
 * whatever trailing bytes haven't seen a '\n' yet so a line split across
 * two chunks (a very real possibility: gnu_make_amiga/vc/vasm/vlink
 * write to the log at their own pace, and a 250ms timer tick can land
 * mid-line) is never handed to the caller as two separate, wrongly
 * truncated lines.
 *
 * This is the one part of the whole async-build/live-tailing design
 * that's fully host-testable (see tests/tst_logtail.cpp) -- there is no
 * dos.library/MUI dependency here at all. */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideLogTail {
    AideStr partial; /* bytes seen since the last '\n', not yet a full line */
} AideLogTail;

typedef void (*AideLogTailLineFn)(const char *line, void *userdata);

void aide_logtail_init(AideLogTail *t);
void aide_logtail_free(AideLogTail *t);

/* Appends `len` new bytes and calls cb(line, userdata) once per complete
 * line found (the terminating '\n' is stripped; a preceding '\r' is
 * stripped too, so CRLF-terminated logs behave the same as LF-only
 * ones). Bytes after the last '\n' in this feed -- if any -- are kept
 * for the next feed()/flush() call, not handed to cb() yet. */
void aide_logtail_feed(AideLogTail *t, const char *chunk, size_t len,
                        AideLogTailLineFn cb, void *userdata);

/* Flushes whatever partial line is still held (non-empty only) as one
 * final line, e.g. once the build process has exited and no further
 * '\n' will ever arrive to terminate it. Resets the held buffer to empty
 * either way. */
void aide_logtail_flush(AideLogTail *t, AideLogTailLineFn cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_LOGTAIL_H */
