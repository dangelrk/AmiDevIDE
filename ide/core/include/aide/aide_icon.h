#ifndef AIDE_ICON_H
#define AIDE_ICON_H

/* Portable C99 mirror of build/IconWriter.h -- writes/patches Workbench
 * .info icons (AmigaOS DiskObject format). See that header for the
 * verified byte layout (78-byte fixed header, do_StackSize as a
 * big-endian LONG at offset 74). On the Amiga build this could instead
 * delegate to icon.library (GetDiskObject/PutDiskObject); this port
 * keeps the from-scratch byte writer so the host build needs no
 * AmigaOS libraries to generate/patch an icon. */

#ifdef __cplusplus
extern "C" {
#endif

/* Copies an existing .info file at sourcePath to destPath, optionally
 * patching its do_StackSize field. Returns 1 on success, 0 on I/O error
 * or if sourcePath is not a valid DiskObject (bad magic/too short). */
int aide_icon_copy(const char *sourcePath, const char *destPath, int patchStackSize, int stackSize);

/* Writes a minimal, from-scratch valid WBTOOL .info (single 24x24
 * monochrome border-box glyph, no tooltypes/defaulttool/drawerdata)
 * with the given stack size. Returns 1 on success. */
int aide_icon_write_default(const char *destPath, int stackSize);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_ICON_H */
