#ifndef AIDE_HUNK_H
#define AIDE_HUNK_H

/* Portable C99 mirror of disasm/HunkReader.h -- parses AmigaOS Hunk-format
 * executables (HUNK_HEADER-led) and simple object files (HUNK_UNIT-led),
 * extracting CODE/DATA/BSS sections with their symbol tables. See that
 * header for the exact hunk-type coverage and verification notes. Pure
 * big-endian byte parsing -- trivial on 68k, which is native big-endian. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AideHunkKind {
    AIDE_HUNK_CODE = 0,
    AIDE_HUNK_DATA = 1,
    AIDE_HUNK_BSS = 2,
} AideHunkKind;

typedef struct AideHunkSymbol {
    char *name;
    unsigned long offset; /* byte offset within the hunk */
} AideHunkSymbol;

typedef struct AideHunkSection {
    AideHunkKind kind;
    int index;                 /* hunk number, for display ("hunk 0") */
    unsigned char *data;       /* malloc'd raw bytes; NULL for Bss */
    size_t dataSize;           /* bytes valid at `data`; 0 for Bss */
    unsigned long sizeBytes;   /* for Bss, the reserved size; else == dataSize */
    AideHunkSymbol *symbols;
    size_t symbolCount;
    size_t symbolCap;
} AideHunkSection;

typedef struct AideHunkReader {
    AideHunkSection *sections;
    size_t sectionCount;
    size_t sectionCap;
    char *error; /* "" if the last load succeeded */
} AideHunkReader;

void aide_hunkreader_init(AideHunkReader *r);
void aide_hunkreader_free(AideHunkReader *r);

/* Parses an in-memory Hunk file/object (HUNK_HEADER- or HUNK_UNIT-led).
 * Clears any previous sections/error first. Returns 1 on success, 0 on
 * error (see r->error). This is the portable entry point; the Qt side's
 * HunkReader::load(path) just reads the file into memory and calls this. */
int aide_hunkreader_load_bytes(AideHunkReader *r, const unsigned char *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_HUNK_H */
