#include "SdkRegistry.h"
#include "SdkDescriptorAide.h"

#include <QCoreApplication>
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

QList<SdkDescriptor> SdkRegistry::builtinAndManifestDescriptors(const QString &toolchainRoot,
                                                                const QMap<QString, QString> &rootOverrides) const
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
    const size_t builtinCount = list.count;
    // Before the manifests, so only a built-in can be moved this way.
    for (auto it = rootOverrides.cbegin(); it != rootOverrides.cend(); ++it)
        aide_sdk_list_set_root(&list, it.key().toUtf8().constData(), it.value().toUtf8().constData());
    aide_sdk_append_from_manifests(sdkRootUtf8.constData(), &list);

    QList<SdkDescriptor> result;
    result.reserve(int(list.count));
    for (size_t i = 0; i < list.count; ++i) {
        SdkDescriptor d = sdkDescriptorFromAide(list.items[i]);
        d.fromManifest = i >= builtinCount;
        result << d;
    }
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
        // A built-in or an AmiDevSDK.json with the same name and version wins.
        for (size_t i = 0; i < list.count; ++i) {
            const SdkDescriptor d = sdkDescriptorFromAide(list.items[i]);
            if (!find(d.id))
                m_sdks << d;
        }
    }
    aide_sdk_list_free(&list);
}

void SdkRegistry::saveUserSdks() const
{
    AideSdkList list;
    aide_sdk_list_init(&list);
    for (const SdkDescriptor &d : m_sdks) {
        if (!d.userDefined || d.fromManifest)
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

void SdkRegistry::reload(const QString &toolchainRoot, const QMap<QString, QString> &rootOverrides)
{
    m_toolchainRoot = toolchainRoot;
    m_sdks = builtinAndManifestDescriptors(toolchainRoot, rootOverrides);
    loadUserSdks();
}

QList<SdkDescriptor> SdkRegistry::defaultBuiltinSdks() const
{
    QList<SdkDescriptor> list;
    for (const SdkDescriptor &d : builtinAndManifestDescriptors(m_toolchainRoot, {})) {
        if (!d.userDefined)
            list << d;
    }
    return list;
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
        if (d.id.compare(id, Qt::CaseInsensitive) == 0)
            return &d;
    }
    return nullptr;
}

void SdkRegistry::toAideList(AideSdkList *out) const
{
    aide_sdk_list_init(out);
    for (const SdkDescriptor &d : m_sdks) {
        AideSdkDescriptor a;
        sdkDescriptorToAide(d, &a);
        aide_sdk_list_push(out, &a);
        aide_sdk_descriptor_free(&a);
    }
}

const SdkDescriptor *SdkRegistry::resolve(const QString &id) const
{
    // The rule lives in aide_sdk.c; run it over a C copy of the list and
    // map the answer back by position.
    AideSdkList list;
    toAideList(&list);
    const QByteArray idUtf8 = id.toUtf8();
    const AideSdkDescriptor *hit = aide_sdk_list_resolve(&list, idUtf8.constData());
    const SdkDescriptor *result = hit ? &m_sdks[int(hit - list.items)] : nullptr;
    aide_sdk_list_free(&list);
    return result;
}

QStringList SdkRegistry::majorUpgradeIds(const QStringList &ids) const
{
    AideSdkList list;
    AideStrList selected, upgrades;
    toAideList(&list);
    aide_strlist_init(&selected);
    aide_strlist_init(&upgrades);
    for (const QString &id : ids) {
        const QByteArray utf8 = id.toUtf8();
        aide_strlist_push(&selected, utf8.constData());
    }
    aide_sdk_list_major_upgrades(&list, &selected, &upgrades);

    QStringList result;
    for (size_t i = 0; i < upgrades.count; ++i)
        result << QString::fromUtf8(upgrades.items[i]);
    aide_strlist_free(&upgrades);
    aide_strlist_free(&selected);
    aide_sdk_list_free(&list);
    return result;
}

QStringList SdkRegistry::missingIds(const QStringList &ids) const
{
    QStringList missing;
    for (const QString &id : ids) {
        if (!resolve(id))
            missing << id;
    }
    return missing;
}

QStringList SdkRegistry::missingParts(const SdkDescriptor &d, bool *rootMissing)
{
    static const char *const kPartLabels[] = {
        QT_TRANSLATE_NOOP("SdkRegistry", "directory"),
        QT_TRANSLATE_NOOP("SdkRegistry", "C includes"),
        QT_TRANSLATE_NOOP("SdkRegistry", "assembler includes"),
        QT_TRANSLATE_NOOP("SdkRegistry", "libraries"),
        QT_TRANSLATE_NOOP("SdkRegistry", "autodocs"),
        QT_TRANSLATE_NOOP("SdkRegistry", "autodoc"),
        QT_TRANSLATE_NOOP("SdkRegistry", "SFD files"),
    };
    AideSdkDescriptor a;
    AideSdkCheck c;
    QStringList lines;

    aide_sdk_descriptor_init(&a);
    sdkDescriptorToAide(d, &a);
    aide_sdk_check_init(&c);
    aide_sdk_check(&a, &c);

    const bool noRoot = c.count > 0 && !c.items[0].present && c.items[0].kind == AIDE_SDK_PART_ROOT;
    for (size_t i = 0; i < c.count; ++i) {
        const AideSdkPart &p = c.items[i];
        if (p.present)
            continue;
        QString line = QCoreApplication::translate("SdkRegistry", "Missing %1: %2")
                           .arg(QCoreApplication::translate("SdkRegistry", kPartLabels[p.kind]),
                                QString::fromUtf8(p.path));
        if (p.similar)
            line += QCoreApplication::translate("SdkRegistry", " -- there is %1, renamed?")
                        .arg(QString::fromUtf8(p.similar));
        lines << line;
        if (noRoot)
            break; // below a missing root every part is missing
    }
    if (rootMissing)
        *rootMissing = noRoot;
    aide_sdk_check_free(&c);
    aide_sdk_descriptor_free(&a);
    return lines;
}

void SdkRegistry::addUserSdk(const SdkDescriptor &descriptor)
{
    const SdkDescriptor *existing = find(descriptor.id);
    if (existing && (!existing->userDefined || existing->fromManifest))
        return;
    SdkDescriptor d = descriptor;
    d.userDefined = true;
    d.fromManifest = false;
    removeUserSdk(d.id);
    m_sdks << d;
    saveUserSdks();
}

void SdkRegistry::removeUserSdk(const QString &id)
{
    for (int i = 0; i < m_sdks.size(); ++i) {
        if (m_sdks[i].id.compare(id, Qt::CaseInsensitive) == 0 && m_sdks[i].userDefined
            && !m_sdks[i].fromManifest) {
            m_sdks.removeAt(i);
            saveUserSdks();
            return;
        }
    }
}
