#pragma once

#include <QString>

// Writes/patches Workbench .info icons (AmigaOS DiskObject format).
//
// Verified against a real .info file (sdk/MUI_3.8/.info) in this repo: the
// fixed DiskObject header is 78 bytes -- magic(2) version(2) Gadget(44)
// do_Type(1) pad(1) do_DefaultTool(4) do_ToolTypes(4) do_CurrentX(4)
// do_CurrentY(4) do_DrawerData(4) do_ToolWindow(4) do_StackSize(4) -- and
// do_StackSize is a plain big-endian LONG living at fixed offset 74,
// independent of whatever image/string data follows the header. Patching it
// in place is safe for any real-world icon (do_Version >= 1, universal since
// OS2.0-era icon.library).
namespace IconWriter {

// Copies an existing .info file to destPath, optionally patching its
// do_StackSize field. Returns false on I/O error or if sourcePath is not a
// valid DiskObject (bad magic).
bool copyIcon(const QString &sourcePath, const QString &destPath, bool patchStackSize, int stackSize);

// Writes a minimal, from-scratch valid WBTOOL .info (single monochrome
// image, no tooltypes/defaulttool/drawerdata) with the given stack size.
bool writeDefaultIcon(const QString &destPath, int stackSize);

}
