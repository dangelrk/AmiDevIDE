#include "ToolchainConfig.h"

#include "aide/aide_toolchain.h"

#include <cstdlib>

// Thin adapter over the portable core/ implementation (core/src/aide_toolchain.c).

ToolchainConfig::ToolchainConfig(const QString &root)
    : m_root(root)
{
}

QString ToolchainConfig::exeSuffix() const
{
    return QString::fromUtf8(aide_toolchain_exe_suffix());
}

namespace {
QString callAndFree(char *(*fn)(const AideToolchainConfig *), const QString &root)
{
    AideToolchainConfig tc;
    const QByteArray rootUtf8 = root.toUtf8();
    aide_toolchain_init(&tc, rootUtf8.constData());
    char *result = fn(&tc);
    const QString qResult = QString::fromUtf8(result);
    free(result);
    aide_toolchain_free(&tc);
    return qResult;
}
}

QString ToolchainConfig::vcPath() const
{
    return callAndFree(aide_toolchain_vc_path, m_root);
}

QString ToolchainConfig::vbccm68kPath() const
{
    return callAndFree(aide_toolchain_vbccm68k_path, m_root);
}

QString ToolchainConfig::vasmPath() const
{
    return callAndFree(aide_toolchain_vasm_path, m_root);
}

QString ToolchainConfig::vlinkPath() const
{
    return callAndFree(aide_toolchain_vlink_path, m_root);
}
