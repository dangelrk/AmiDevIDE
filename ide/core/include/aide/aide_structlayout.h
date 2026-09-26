#ifndef AIDE_STRUCTLAYOUT_H
#define AIDE_STRUCTLAYOUT_H

/* Portable C99 mirror of index/StructLayout.h -- computes AmigaOS
 * (m68k/vbcc natural alignment: only 2-byte word alignment, never 4-byte)
 * struct layouts from a flat, in-order member list. See that header for
 * the exact alignment rules this implements. */

#include "aide/aide_hash.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideMemberDecl {
    char *type; /* declared type text, e.g. "APTR", "struct Node *" */
    char *name; /* declarator, optionally with a trailing "[N]" suffix */
} AideMemberDecl;

typedef struct AideMemberLayout {
    char *name; /* array suffix stripped */
    char *type;
    int offset;
    int size; /* 0 if the type couldn't be resolved */
} AideMemberLayout;

typedef struct AideStructLayoutResult {
    AideMemberLayout *members;
    size_t memberCount;
    size_t memberCap;
    int totalSize;
} AideStructLayoutResult;

void aide_structlayout_result_init(AideStructLayoutResult *r);
void aide_structlayout_result_free(AideStructLayoutResult *r);

/* `knownStructSizes` supplies sizes (name -> bytes) for embedded structs
 * whose tag isn't a built-in Amiga base type -- see aide_structlayout.h's
 * Qt counterpart for the exact resolution rules. Clears and fills *out. */
void aide_structlayout_compute(const AideMemberDecl *members, size_t memberCount,
                                const AideStrIntMap *knownStructSizes,
                                AideStructLayoutResult *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_STRUCTLAYOUT_H */
