#ifndef AIDE_SFD_H
#define AIDE_SFD_H

/* Portable C99 mirror of index/SfdIndex.h -- Amiga function name -> library
 * + prototype, parsed from *_lib.sfd files: "==libname"/"==base"
 * directives, then C prototypes with a trailing register-allocation list,
 * with tab/space-indented continuation lines for long parameter lists.
 * Used for Hothelp routing, signature tooltips, and completion. */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideSfdFunction {
    char *name;
    char *library;      /* e.g. "exec.library" */
    char *rawSignature;  /* full "ReturnType Name(params)" text */
    char *registers;     /* raw contents of the trailing register list */
} AideSfdFunction;

typedef struct AideSfdIndex {
    AideSfdFunction *items;
    size_t count;
    size_t cap;
} AideSfdIndex;

void aide_sfdindex_init(AideSfdIndex *idx);
void aide_sfdindex_free(AideSfdIndex *idx);

/* Clears the index, then re-scans every *.sfd file directly inside each
 * of `sfdDirs` (not recursive, matching QDir::entryList's default). */
void aide_sfdindex_reload(AideSfdIndex *idx, const AideStrList *sfdDirs);

const AideSfdFunction *aide_sfdindex_find(const AideSfdIndex *idx, const char *functionName);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_SFD_H */
