#include "IconWriter.h"

#include "aide/aide_icon.h"

// Thin adapter over the portable core/ implementation (core/src/aide_icon.c,
// shared with the Amiga/MUI build).

namespace IconWriter {

bool copyIcon(const QString &sourcePath, const QString &destPath, bool patchStackSize, int stackSize)
{
    const QByteArray srcUtf8 = sourcePath.toUtf8();
    const QByteArray dstUtf8 = destPath.toUtf8();
    return aide_icon_copy(srcUtf8.constData(), dstUtf8.constData(), patchStackSize ? 1 : 0, stackSize) != 0;
}

bool writeDefaultIcon(const QString &destPath, int stackSize)
{
    const QByteArray dstUtf8 = destPath.toUtf8();
    return aide_icon_write_default(dstUtf8.constData(), stackSize) != 0;
}

} // namespace IconWriter
