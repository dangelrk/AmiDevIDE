#include "aide/aide_icon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_OBJECT_MAGIC 0xE310U
#define STACK_SIZE_OFFSET 74 /* verified against sdk/MUI_3.8/.info */

typedef struct ByteBuf {
    unsigned char *data;
    size_t len;
    size_t cap;
} ByteBuf;

static void buf_init(ByteBuf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_free(ByteBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_reserve(ByteBuf *b, size_t minCap)
{
    size_t newCap;
    if (b->cap >= minCap)
        return;
    newCap = b->cap > 0 ? b->cap : 64;
    while (newCap < minCap)
        newCap *= 2;
    b->data = (unsigned char *)realloc(b->data, newCap);
    b->cap = newCap;
}

static void buf_append_byte(ByteBuf *b, unsigned char v)
{
    buf_reserve(b, b->len + 1);
    b->data[b->len++] = v;
}

static void buf_append_bytes(ByteBuf *b, const unsigned char *src, size_t n)
{
    buf_reserve(b, b->len + n);
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void buf_append_be16(ByteBuf *b, unsigned int v)
{
    buf_append_byte(b, (unsigned char)((v >> 8) & 0xFF));
    buf_append_byte(b, (unsigned char)(v & 0xFF));
}

static void buf_append_be32(ByteBuf *b, unsigned long v)
{
    buf_append_byte(b, (unsigned char)((v >> 24) & 0xFF));
    buf_append_byte(b, (unsigned char)((v >> 16) & 0xFF));
    buf_append_byte(b, (unsigned char)((v >> 8) & 0xFF));
    buf_append_byte(b, (unsigned char)(v & 0xFF));
}

static int raster_bytes_per_row(int width)
{
    return ((width + 15) / 16) * 2;
}

static int read_whole_file(const char *path, ByteBuf *out)
{
    FILE *f = fopen(path, "rb");
    long size;
    size_t readCount;

    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return 0;
    }

    buf_init(out);
    buf_reserve(out, (size_t)size);
    readCount = fread(out->data, 1, (size_t)size, f);
    fclose(f);
    out->len = readCount;
    return 1;
}

static int write_whole_file(const char *path, const unsigned char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t written;
    if (!f)
        return 0;
    written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len;
}

int aide_icon_copy(const char *sourcePath, const char *destPath, int patchStackSize, int stackSize)
{
    ByteBuf data;
    unsigned int magic;
    int ok;

    if (!read_whole_file(sourcePath, &data))
        return 0;

    if (data.len < 78) {
        buf_free(&data);
        return 0;
    }

    magic = ((unsigned int)data.data[0] << 8) | (unsigned int)data.data[1];
    if (magic != DISK_OBJECT_MAGIC) {
        buf_free(&data);
        return 0;
    }

    if (patchStackSize) {
        data.data[STACK_SIZE_OFFSET + 0] = (unsigned char)((stackSize >> 24) & 0xFF);
        data.data[STACK_SIZE_OFFSET + 1] = (unsigned char)((stackSize >> 16) & 0xFF);
        data.data[STACK_SIZE_OFFSET + 2] = (unsigned char)((stackSize >> 8) & 0xFF);
        data.data[STACK_SIZE_OFFSET + 3] = (unsigned char)(stackSize & 0xFF);
    }

    ok = write_whole_file(destPath, data.data, data.len);
    buf_free(&data);
    return ok;
}

int aide_icon_write_default(const char *destPath, int stackSize)
{
    const int kWidth = 24;
    const int kHeight = 24;
    const int kDepth = 2;
    const int bytesPerRow = raster_bytes_per_row(kWidth);
    ByteBuf out;
    unsigned char *plane0, *plane1;
    int x, y;
    int ok;

    buf_init(&out);
    buf_reserve(&out, (size_t)(78 + 20 + bytesPerRow * kHeight * kDepth));

    /* --- DiskObject fixed header (78 bytes) --- */
    buf_append_be16(&out, DISK_OBJECT_MAGIC);
    buf_append_be16(&out, 1); /* do_Version */

    /* struct Gadget (44 bytes) */
    buf_append_be32(&out, 0);              /* NextGadget */
    buf_append_be16(&out, 0);              /* LeftEdge */
    buf_append_be16(&out, 0);              /* TopEdge */
    buf_append_be16(&out, (unsigned int)kWidth);
    buf_append_be16(&out, (unsigned int)kHeight);
    buf_append_be16(&out, 0x0006);         /* Flags: GFLG_GADGIMAGE | GFLG_GADGHCOMP */
    buf_append_be16(&out, 0);              /* Activation */
    buf_append_be16(&out, 0x0001);         /* GadgetType */
    buf_append_be32(&out, 1);              /* GadgetRender (nonzero => one Image follows) */
    buf_append_be32(&out, 0);              /* SelectRender (none => highlight via GADGHCOMP) */
    buf_append_be32(&out, 0);              /* GadgetText */
    buf_append_be32(&out, 0);              /* MutualExclude */
    buf_append_be32(&out, 0);              /* SpecialInfo */
    buf_append_be16(&out, 0);              /* GadgetID */
    buf_append_be32(&out, 0);              /* UserData */

    buf_append_byte(&out, 3);              /* do_Type = WBTOOL */
    buf_append_byte(&out, 0);              /* pad */

    buf_append_be32(&out, 0);              /* do_DefaultTool (none) */
    buf_append_be32(&out, 0);              /* do_ToolTypes (none) */
    buf_append_be32(&out, 0xFFFFFFFFUL);   /* do_CurrentX (-1 = auto-place) */
    buf_append_be32(&out, 0xFFFFFFFFUL);   /* do_CurrentY (-1 = auto-place) */
    buf_append_be32(&out, 0);              /* do_DrawerData (none) */
    buf_append_be32(&out, 0);              /* do_ToolWindow (none) */
    buf_append_be32(&out, (unsigned long)(unsigned int)stackSize); /* do_StackSize */

    /* --- struct Image (20 bytes) --- */
    buf_append_be16(&out, 0);   /* LeftEdge */
    buf_append_be16(&out, 0);   /* TopEdge */
    buf_append_be16(&out, (unsigned int)kWidth);
    buf_append_be16(&out, (unsigned int)kHeight);
    buf_append_be16(&out, (unsigned int)kDepth);
    buf_append_be32(&out, 1);   /* ImageData (placeholder; data follows positionally) */
    buf_append_byte(&out, 0x03); /* PlanePick: both planes are real data */
    buf_append_byte(&out, 0x00); /* PlaneOnOff */
    buf_append_be32(&out, 0);   /* NextImage */

    /* --- raw bitplane data: a simple border-box glyph --- */
    plane0 = (unsigned char *)calloc((size_t)(bytesPerRow * kHeight), 1);
    plane1 = (unsigned char *)calloc((size_t)(bytesPerRow * kHeight), 1);
    for (y = 0; y < kHeight; ++y) {
        for (x = 0; x < kWidth; ++x) {
            const int border = (x == 0 || y == 0 || x == kWidth - 1 || y == kHeight - 1);
            if (border) {
                const int byteIndex = y * bytesPerRow + x / 8;
                const int bit = 7 - (x % 8);
                plane0[byteIndex] = (unsigned char)(plane0[byteIndex] | (1 << bit));
            }
        }
    }
    buf_append_bytes(&out, plane0, (size_t)(bytesPerRow * kHeight));
    buf_append_bytes(&out, plane1, (size_t)(bytesPerRow * kHeight));
    free(plane0);
    free(plane1);

    ok = write_whole_file(destPath, out.data, out.len);
    buf_free(&out);
    return ok;
}
