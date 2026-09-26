#include "SdkRegistry.h"
#include "SdkDescriptorAide.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <cstdlib>

// Thin adapter over the portable core/ implementation (core/src/aide_sdk.c,
// shared with the Amiga/MUI build).
// The 8 built-in descriptors and the sdks.json array (de)serialization now
// live in aide_sdk.c; this file only owns file-path resolution (Qt-specific)
// and converts through AideSdkDescriptor/AideSdkList.

SdkRegistry &SdkRegistry::instance()
{
    static SdkRegistry registry;
    return registry;
}

QList<SdkDescriptor> SdkRegistry::builtinDescriptors(const QString &toolchainRoot) const
{
    AideSdkList list;
    aide_sdk_list_init(&list);
    // aide_sdk_append_builtins() takes the SDK root (the directory directly
    // containing NDK_3.2/, MUI_3.8/, ...), not the toolchain root -- on the
    // Qt/host side that's toolchainRoot + "/sdk" (on the native Amiga side
    // it's the independent "SDK:" assign, see mainwindow.c).
    const QString sdkRoot = toolchainRoot + QStringLiteral("/sdk");
    const QByteArray sdkRootUtf8 = sdkRoot.toUtf8();
    aide_sdk_append_builtins(sdkRootUtf8.constData(), &list);

    QList<SdkDescriptor> result;
    result.reserve(int(list.count));
    for (size_t i = 0; i < list.count; ++i)
        result << sdkDescriptorFromAide(list.items[i]);
    aide_sdk_list_free(&list);
    return result;
}

QString SdkRegistry::userSdksJsonPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/sdks.json");
}

void SdkRegistry::loadUserSdks()
{
    QFile file(userSdksJsonPath());
    if (!file.open(QFile::ReadOnly))
        return;

    const QByteArray json = file.readAll();
    AideSdkList list;
    aide_sdk_list_init(&list);
    if (aide_sdk_list_append_from_json_array(&list, json.constData())) {
        for (size_t i = 0; i < list.count; ++i)
            m_sdks << sdkDescriptorFromAide(list.items[i]);
    }
    aide_sdk_list_free(&list);
}

void SdkRegistry::saveUserSdks() const
{
    AideSdkList list;
    aide_sdk_list_init(&list);
    for (const SdkDescriptor &d : m_sdks) {
        if (!d.userDefined)
            continue;
        AideSdkDescriptor a;
        sdkDescriptorToAide(d, &a);
        aide_sdk_list_push(&list, &a);
        aide_sdk_descriptor_free(&a);
    }

    char *json = aide_sdk_list_to_json_array(&list);
    aide_sdk_list_free(&list);

    QFile file(userSdksJsonPath());
    if (file.open(QFile::WriteOnly))
        file.write(json);
    free(json);
}

void SdkRegistry::reload(const QString &toolchainRoot)
{
    m_toolchainRoot = toolchainRoot;
    m_sdks = builtinDescriptors(toolchainRoot);
    loadUserSdks();
}

QList<SdkDescriptor> SdkRegistry::builtinSdks() const
{
    QList<SdkDescriptor> list;
    for (const SdkDescriptor &d : m_sdks) {
        if (!d.userDefined)
            list << d;
    }
    return list;
}

QList<SdkDescriptor> SdkRegistry::userSdks() const
{
    QList<SdkDescriptor> list;
    for (const SdkDescriptor &d : m_sdks) {
        if (d.userDefined)
            list << d;
    }
    return list;
}

const SdkDescriptor *SdkRegistry::find(const QString &id) const
{
    for (const SdkDescriptor &d : m_sdks) {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

void SdkRegistry::addUserSdk(const SdkDescriptor &descriptor)
{
    SdkDescriptor d = descriptor;
    d.userDefined = true;
    removeUserSdk(d.id);
    m_sdks << d;
    saveUserSdks();
}

void SdkRegistry::removeUserSdk(const QString &id)
{
    for (int i = 0; i < m_sdks.size(); ++i) {
        if (m_sdks[i].id == id && m_sdks[i].userDefined) {
            m_sdks.removeAt(i);
            saveUserSdks();
            return;
        }
    }
}
