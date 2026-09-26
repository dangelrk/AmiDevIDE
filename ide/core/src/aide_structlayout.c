#include "aide/aide_structlayout.h"
#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

static const struct { const char *name; int size; } kBaseTypes[] = {
    { "BYTE", 1 }, { "UBYTE", 1 },
    { "char", 1 }, { "unsigned char", 1 },
    { "BOOL", 1 }, { "TEXT", 1 },

    { "WORD", 2 }, { "UWORD", 2 },
    { "SHORT", 2 }, { "USHORT", 2 },
    { "short", 2 }, { "unsigned short", 2 },

    { "LONG", 4 }, { "ULONG", 4 },
    { "APTR", 4 }, { "BPTR", 4 }, { "CPTR", 4 },
    { "int", 4 }, { "unsigned int", 4 }, { "unsigned", 4 },
    { "long", 4 }, { "unsigned long", 4 },
    { "float", 4 },

    { "double", 8 }, /* still only needs 2-byte alignment on this ABI */
};

static int base_type_size(const char *type)
{
    size_t i;
    for (i = 0; i < sizeof(kBaseTypes) / sizeof(kBaseTypes[0]); ++i) {
        if (strcmp(kBaseTypes[i].name, type) == 0)
            return kBaseTypes[i].size;
    }
    return -1;
}

static AideRegex *s_arrayRe = NULL;
static AideRegex *s_tagRe = NULL;

static void ensure_compiled(void)
{
    if (!s_arrayRe)
        s_arrayRe = aide_regex_compile("^([A-Za-z0-9_]+)[[:space:]]*\\[[[:space:]]*([0-9]+)[[:space:]]*\\]$", 0);
    if (!s_tagRe)
        s_tagRe = aide_regex_compile("^(struct|union)[[:space:]]+([A-Za-z0-9_]+)$", 0);
}

/* Splits a trailing "[N]" array suffix off `name`, returning the element
 * count (1 if there's no array suffix) and writing the bare name (freshly
 * malloc'd) to *outBase. */
static int array_count_and_base_name(const char *name, char **outBase)
{
    AideRegexMatch m;
    ensure_compiled();
    if (aide_regex_exec(s_arrayRe, name, 0, &m)) {
        *outBase = aide_strndup(name + m.start[1], (size_t)(m.end[1] - m.start[1]));
        return (int)strtol(name + m.start[2], NULL, 10);
    }
    *outBase = aide_strdup(name);
    return 1;
}

static int size_of_member(const char *rawType, const char *rawName, const AideStrIntMap *knownStructSizes,
                           char **outBaseName)
{
    char *type = aide_str_trim_dup(rawType);
    char *trimmedName = aide_str_trim_dup(rawName);
    int arrayCount = array_count_and_base_name(trimmedName, outBaseName);
    int result;

    free(trimmedName);

    /* Any level of indirection is a plain 4-byte Amiga pointer. */
    if (strchr(type, '*') != NULL) {
        free(type);
        return 4 * arrayCount;
    }

    {
        AideRegexMatch m;
        ensure_compiled();
        if (aide_regex_exec(s_tagRe, type, 0, &m)) {
            char *tag = aide_strndup(type + m.start[2], (size_t)(m.end[2] - m.start[2]));
            result = aide_strintmap_get(knownStructSizes, tag, 0) * arrayCount;
            free(tag);
            free(type);
            return result;
        }
    }

    {
        const int base = base_type_size(type);
        if (base > 0) {
            free(type);
            return base * arrayCount;
        }
    }

    /* Not a recognized base type or "struct X" -- maybe a typedef'd struct
     * name used without the "struct" keyword (e.g. "Node mn_Node;"). */
    result = aide_strintmap_get(knownStructSizes, type, 0) * arrayCount;
    free(type);
    return result;
}

void aide_structlayout_result_init(AideStructLayoutResult *r)
{
    r->members = NULL;
    r->memberCount = 0;
    r->memberCap = 0;
    r->totalSize = 0;
}

void aide_structlayout_result_free(AideStructLayoutResult *r)
{
    size_t i;
    for (i = 0; i < r->memberCount; ++i) {
        free(r->members[i].name);
        free(r->members[i].type);
    }
    free(r->members);
    aide_structlayout_result_init(r);
}

static AideMemberLayout *push_member(AideStructLayoutResult *r)
{
    if (r->memberCount >= r->memberCap) {
        size_t newCap = r->memberCap > 0 ? r->memberCap * 2 : 8;
        r->members = (AideMemberLayout *)realloc(r->members, newCap * sizeof(AideMemberLayout));
        r->memberCap = newCap;
    }
    return &r->members[r->memberCount++];
}

void aide_structlayout_compute(const AideMemberDecl *members, size_t memberCount,
                                const AideStrIntMap *knownStructSizes, AideStructLayoutResult *out)
{
    size_t i;
    int offset = 0;

    aide_structlayout_result_free(out);

    for (i = 0; i < memberCount; ++i) {
        char *name;
        const int size = size_of_member(members[i].type, members[i].name, knownStructSizes, &name);
        const int align = (size == 1) ? 1 : 2; /* Amiga structs never need more than 2-byte alignment */
        AideMemberLayout *layout;

        if (align > 1 && offset % align != 0)
            ++offset;

        layout = push_member(out);
        layout->name = name;
        layout->type = aide_strdup(members[i].type);
        layout->offset = offset;
        layout->size = size;

        offset += size;
    }

    if (offset % 2 != 0)
        ++offset;
    out->totalSize = offset;
}
