#include "aide/aide_hunk.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

#define HUNK_UNIT 0x3E7UL
#define HUNK_NAME 0x3E8UL
#define HUNK_CODE 0x3E9UL
#define HUNK_DATA 0x3EAUL
#define HUNK_BSS 0x3EBUL
#define HUNK_RELOC32 0x3ECUL
#define HUNK_EXT 0x3EFUL
#define HUNK_SYMBOL 0x3F0UL
#define HUNK_DEBUG 0x3F1UL
#define HUNK_END 0x3F2UL
#define HUNK_HEADER 0x3F3UL

/* EXT entry types actually observed in vasm/vbcc-produced object files
 * (verified against a real .o: _printf as EXT_REF32, _add/_main as EXT_DEF). */
#define EXT_DEF 0x01UL
#define EXT_ABS 0x02UL
/* EXT_REF32 (0x81) and other reference types (>= 0x80) share a
 * count+offsets payload shape, handled generically below. */

static unsigned long read_u32(const unsigned char *data, size_t pos)
{
    return ((unsigned long)data[pos] << 24) | ((unsigned long)data[pos + 1] << 16)
         | ((unsigned long)data[pos + 2] << 8) | (unsigned long)data[pos + 3];
}

static int is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void set_error(AideHunkReader *r, const char *text)
{
    free(r->error);
    r->error = aide_strdup(text);
}

static void set_error_fmt(AideHunkReader *r, const char *fmt, unsigned long a, unsigned long b)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_appendf(&s, fmt, a, b);
    free(r->error);
    r->error = aide_str_take(&s);
}

static AideHunkSection *push_section(AideHunkReader *r)
{
    if (r->sectionCount >= r->sectionCap) {
        size_t newCap = r->sectionCap > 0 ? r->sectionCap * 2 : 4;
        r->sections = (AideHunkSection *)realloc(r->sections, newCap * sizeof(AideHunkSection));
        r->sectionCap = newCap;
    }
    memset(&r->sections[r->sectionCount], 0, sizeof(AideHunkSection));
    return &r->sections[r->sectionCount++];
}

static void push_symbol(AideHunkSection *section, const char *name, unsigned long offset)
{
    if (section->symbolCount >= section->symbolCap) {
        size_t newCap = section->symbolCap > 0 ? section->symbolCap * 2 : 4;
        section->symbols = (AideHunkSymbol *)realloc(section->symbols, newCap * sizeof(AideHunkSymbol));
        section->symbolCap = newCap;
    }
    section->symbols[section->symbolCount].name = aide_strdup(name);
    section->symbols[section->symbolCount].offset = offset;
    section->symbolCount++;
}

static void free_section(AideHunkSection *section)
{
    size_t i;
    free(section->data);
    for (i = 0; i < section->symbolCount; ++i)
        free(section->symbols[i].name);
    free(section->symbols);
    memset(section, 0, sizeof(*section));
}

void aide_hunkreader_init(AideHunkReader *r)
{
    r->sections = NULL;
    r->sectionCount = 0;
    r->sectionCap = 0;
    r->error = aide_strdup("");
}

static void clear_sections(AideHunkReader *r)
{
    size_t i;
    for (i = 0; i < r->sectionCount; ++i)
        free_section(&r->sections[i]);
    r->sectionCount = 0;
}

void aide_hunkreader_free(AideHunkReader *r)
{
    clear_sections(r);
    free(r->sections);
    r->sections = NULL;
    r->sectionCap = 0;
    free(r->error);
    r->error = NULL;
}

/* Returns 1 on success (HUNK_END reached), 0 on error (r->error set). */
static int parse_hunk_body(AideHunkReader *r, const unsigned char *data, size_t size, size_t *posPtr,
                            AideHunkSection *section)
{
    size_t pos = *posPtr;

    while (pos + 4 <= size) {
        const unsigned long id = read_u32(data, pos);
        pos += 4;

        if (id == HUNK_CODE || id == HUNK_DATA) {
            unsigned long lengthLongs, lengthBytes;
            section->kind = id == HUNK_CODE ? AIDE_HUNK_CODE : AIDE_HUNK_DATA;
            lengthLongs = read_u32(data, pos);
            pos += 4;
            lengthBytes = lengthLongs * 4;
            if (pos + lengthBytes > size) {
                set_error(r, "Truncated CODE/DATA hunk");
                return 0;
            }
            free(section->data);
            section->data = (unsigned char *)malloc(lengthBytes > 0 ? lengthBytes : 1);
            memcpy(section->data, data + pos, lengthBytes);
            section->dataSize = lengthBytes;
            section->sizeBytes = lengthBytes;
            pos += lengthBytes;
        } else if (id == HUNK_BSS) {
            section->kind = AIDE_HUNK_BSS;
            section->sizeBytes = read_u32(data, pos) * 4;
            pos += 4;
        } else if (id == HUNK_RELOC32) {
            for (;;) {
                const unsigned long count = read_u32(data, pos);
                pos += 4;
                if (count == 0)
                    break;
                pos += 4;           /* referenced hunk number */
                pos += count * 4;   /* offsets */
            }
        } else if (id == HUNK_EXT) {
            for (;;) {
                unsigned long typeAndLen, type, nameLenLongs;
                typeAndLen = read_u32(data, pos);
                pos += 4;
                if (typeAndLen == 0)
                    break;
                type = (typeAndLen >> 24) & 0xFFUL;
                nameLenLongs = typeAndLen & 0xFFFFFFUL;
                pos += nameLenLongs * 4; /* name itself not needed here (HUNK_SYMBOL covers display names) */
                if (type == EXT_DEF || type == EXT_ABS) {
                    pos += 4; /* value */
                } else if (type >= 0x80) {
                    const unsigned long count = read_u32(data, pos);
                    pos += 4;
                    pos += count * 4; /* reference offsets */
                } else {
                    set_error_fmt(r, "Unsupported HUNK_EXT entry type %lu", type, 0);
                    return 0;
                }
            }
        } else if (id == HUNK_SYMBOL) {
            for (;;) {
                unsigned long nameLenLongs, offset;
                size_t rawLen, start, end, nulPos;
                const char *raw;
                char *name;

                nameLenLongs = read_u32(data, pos);
                pos += 4;
                if (nameLenLongs == 0)
                    break;
                rawLen = (size_t)nameLenLongs * 4;
                raw = (const char *)(data + pos);
                pos += rawLen;
                offset = read_u32(data, pos);
                pos += 4;

                /* Mirrors QString::fromLatin1(raw).trimmed() followed by
                 * truncate-at-first-embedded-NUL: trim ASCII whitespace
                 * from both ends of the raw (NUL-padded) byte range --
                 * NUL itself is not whitespace, so padding survives the
                 * trim -- then cut at the first NUL byte still left. */
                start = 0;
                end = rawLen;
                while (start < end && is_ascii_space(raw[start]))
                    ++start;
                while (end > start && is_ascii_space(raw[end - 1]))
                    --end;
                nulPos = start;
                while (nulPos < end && raw[nulPos] != '\0')
                    ++nulPos;

                name = aide_strndup(raw + start, nulPos - start);
                push_symbol(section, name, offset);
                free(name);
            }
        } else if (id == HUNK_DEBUG) {
            const unsigned long lengthLongs = read_u32(data, pos);
            pos += 4;
            pos += lengthLongs * 4;
        } else if (id == HUNK_END) {
            *posPtr = pos;
            return 1;
        } else {
            set_error_fmt(r, "Unsupported hunk type %lu at offset %lu", id, (unsigned long)(pos - 4));
            return 0;
        }
    }

    set_error(r, "Missing HUNK_END");
    return 0;
}

static int parse_executable(AideHunkReader *r, const unsigned char *data, size_t size)
{
    size_t pos = 4; /* skip HUNK_HEADER id (already checked by caller) */
    unsigned long firstHunk, lastHunk;
    unsigned long *hunkSizesLongs;
    int hunkCount, i;

    /* Resident library name list, terminated by a zero length. */
    for (;;) {
        const unsigned long len = read_u32(data, pos);
        pos += 4;
        if (len == 0)
            break;
        pos += len * 4;
    }

    pos += 4; /* table size -- unused, matches HunkReader.cpp's Q_UNUSED(tableSize) */
    firstHunk = read_u32(data, pos); pos += 4;
    lastHunk = read_u32(data, pos); pos += 4;

    hunkCount = (int)(lastHunk - firstHunk + 1);
    hunkSizesLongs = (unsigned long *)malloc(sizeof(unsigned long) * (size_t)(hunkCount > 0 ? hunkCount : 1));
    for (i = 0; i < hunkCount; ++i) {
        hunkSizesLongs[i] = read_u32(data, pos) & 0x3FFFFFFFUL; /* low 30 bits; top 2 bits are memory-type flags */
        pos += 4;
    }
    free(hunkSizesLongs);

    for (i = 0; i < hunkCount; ++i) {
        unsigned long id;
        AideHunkSection *section;

        if (pos + 4 > size) {
            set_error_fmt(r, "Truncated file: expected hunk %lu", (unsigned long)i, 0);
            return 0;
        }
        id = read_u32(data, pos);
        pos += 4;
        if (id != HUNK_CODE && id != HUNK_DATA && id != HUNK_BSS) {
            set_error_fmt(r, "Expected CODE/DATA/BSS for hunk %lu, got %lu", (unsigned long)i, id);
            return 0;
        }
        pos -= 4; /* let parse_hunk_body re-read the id */

        section = push_section(r);
        section->index = i;
        if (!parse_hunk_body(r, data, size, &pos, section))
            return 0;
    }

    return 1;
}

static int parse_unit(AideHunkReader *r, const unsigned char *data, size_t size)
{
    size_t pos = 4; /* skip HUNK_UNIT id */
    unsigned long unitNameLenLongs;
    int hunkIndex = 0;

    unitNameLenLongs = read_u32(data, pos);
    pos += 4;
    pos += unitNameLenLongs * 4;

    while (pos + 4 <= size) {
        unsigned long id = read_u32(data, pos);
        AideHunkSection *section;

        if (id == HUNK_NAME) {
            unsigned long nameLenLongs;
            pos += 4;
            nameLenLongs = read_u32(data, pos);
            pos += 4;
            pos += nameLenLongs * 4;
            continue;
        }
        if (id != HUNK_CODE && id != HUNK_DATA && id != HUNK_BSS)
            break; /* next unit, or end of file */

        section = push_section(r);
        section->index = hunkIndex++;
        if (!parse_hunk_body(r, data, size, &pos, section))
            return 0;
    }

    return 1;
}

int aide_hunkreader_load_bytes(AideHunkReader *r, const unsigned char *data, size_t size)
{
    unsigned long firstId;

    clear_sections(r);
    set_error(r, "");

    if (size < 4) {
        set_error(r, "File too small to be a Hunk file");
        return 0;
    }

    firstId = read_u32(data, 0);
    if (firstId == HUNK_HEADER)
        return parse_executable(r, data, size);
    if (firstId == HUNK_UNIT)
        return parse_unit(r, data, size);

    set_error_fmt(r, "Not an AmigaOS Hunk executable or object file (first hunk id %lu)", firstId, 0);
    return 0;
}
