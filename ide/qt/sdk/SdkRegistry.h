#pragma once

#include "SdkDescriptor.h"

#include <QList>
#include <QString>

// Built-in SDK descriptors for the shipped sdk/ tree (paths verified against
// setenv_mac.sh, the toolchain maintainers' own ground truth) plus
// user-defined SDKs persisted in sdks.json under AppDataLocation.
//
// Note (verified on disk): AmiSSL, MUI 5.0 and SQLite3 ship their libraries
// as GCC .a archives (bebbo-gcc format), not Amiga-hunk .lib files, so they
// are not directly usable as vlink link inputs. Their include/autodoc/sfd
// data is still valid and indexed; libDirs are still exposed (matching
// setenv_mac.sh) but suggestedLibs is deliberately left empty for them.
class SdkRegistry {
public:
    static SdkRegistry &instance();

    // toolchainRoot: the amiga/ tree root, i.e. AppSettings::toolchainRoot().
    void reload(const QString &toolchainRoot);

    QList<SdkDescriptor> allSdks() const { return m_sdks; }
    QList<SdkDescriptor> builtinSdks() const;
    QList<SdkDescriptor> userSdks() const;

    const SdkDescriptor *find(const QString &id) const;

    void addUserSdk(const SdkDescriptor &descriptor);
    void removeUserSdk(const QString &id);

private:
    SdkRegistry() = default;

    QList<SdkDescriptor> builtinDescriptors(const QString &toolchainRoot) const;
    QString userSdksJsonPath() const;
    void loadUserSdks();
    void saveUserSdks() const;

    QString m_toolchainRoot;
    QList<SdkDescriptor> m_sdks;
};
