#ifndef AIDE_MUI_REQUIREMENTS_H
#define AIDE_MUI_REQUIREMENTS_H

/* The libraries and MUI classes AmigaIDE needs, each with the Aminet
 * package that provides it. One table for the startup check (main.c,
 * which refuses to start without them) and for Projekt > Installation
 * pruefen... (checkwindow.c, which shows their versions). */

#include <exec/types.h>
#include <stddef.h>

typedef struct LibReq {
    const char *name;     /* as OpenLibrary() wants it */
    const char *altPath;  /* second place to look, or NULL */
    UWORD minVersion;
    const char *package;  /* Aminet path, shown when it's missing/old */
} LibReq;

extern const LibReq kAideLibReqs[];
extern const size_t kAideLibReqCount;

#endif /* AIDE_MUI_REQUIREMENTS_H */
