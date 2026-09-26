#pragma once

#include <QString>

// Resolves the fixed, per-OS toolchain paths under a toolchain root
// (verified ground truth: bin/{vc,vasmm68k_mot,vlink}, targets/m68k-amigaos).
class ToolchainConfig {
public:
    explicit ToolchainConfig(const QString &root);

    QString root() const { return m_root; }
    QString exeSuffix() const;

    QString vcPath() const;
    QString vbccm68kPath() const;
    QString vasmPath() const;
    QString vlinkPath() const;

    QString vbccIncludeDir() const { return m_root + QStringLiteral("/targets/m68k-amigaos/include"); }
    QString vbccLibDir() const { return m_root + QStringLiteral("/targets/m68k-amigaos/lib"); }

private:
    QString m_root;
};
