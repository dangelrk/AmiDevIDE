#include "SdkDescriptor.h"
#include "SdkDescriptorAide.h"

#include <QJsonDocument>

#include <cstdlib>
#include <cstring>

// Thin adapter over the portable core/ implementation (core/src/aide_sdk.c,
// shared with the Amiga/MUI build).
// JSON schema and field defaults live in aide_sdk.c now; this file only
// converts through the C struct so it can keep returning a QJsonObject
// (SdkRegistry.cpp embeds it into sdks.json).

namespace {

QStringList toQStringList(const AideStrList &list)
{
    QStringList out;
    out.reserve(int(list.count));
    for (size_t i = 0; i < list.count; ++i)
        out << QString::fromUtf8(list.items[i]);
    return out;
}

void setAideStr(char **field, const QString &value)
{
    free(*field);
    const QByteArray utf8 = value.toUtf8();
    *field = static_cast<char *>(malloc(size_t(utf8.size()) + 1));
    memcpy(*field, utf8.constData(), size_t(utf8.size()) + 1);
}

void setAideList(AideStrList *list, const QStringList &in)
{
    aide_strlist_clear(list);
    for (const QString &s : in) {
        const QByteArray utf8 = s.toUtf8();
        aide_strlist_push(list, utf8.constData());
    }
}

} // namespace

void sdkDescriptorToAide(const SdkDescriptor &d, AideSdkDescriptor *a)
{
    aide_sdk_descriptor_init(a);
    setAideStr(&a->id, d.id);
    setAideStr(&a->displayName, d.displayName);
    a->kind = d.kind == SdkKind::Ndk ? AIDE_SDK_NDK : d.kind == SdkKind::Mui ? AIDE_SDK_MUI : AIDE_SDK_OTHER;
    setAideStr(&a->version, d.version);
    setAideStr(&a->rootDir, d.rootDir);
    setAideList(&a->includeDirs, d.includeDirs);
    setAideList(&a->asmIncludeDirs, d.asmIncludeDirs);
    setAideList(&a->libDirs, d.libDirs);
    setAideList(&a->autodocDirs, d.autodocDirs);
    setAideList(&a->autodocFiles, d.autodocFiles);
    setAideList(&a->sfdDirs, d.sfdDirs);
    setAideList(&a->suggestedLibs, d.suggestedLibs);
    a->userDefined = d.userDefined ? 1 : 0;
}

SdkDescriptor sdkDescriptorFromAide(const AideSdkDescriptor &a)
{
    SdkDescriptor d;
    d.id = QString::fromUtf8(a.id);
    d.displayName = QString::fromUtf8(a.displayName);
    d.kind = a.kind == AIDE_SDK_NDK ? SdkKind::Ndk : a.kind == AIDE_SDK_MUI ? SdkKind::Mui : SdkKind::Other;
    d.version = QString::fromUtf8(a.version);
    d.rootDir = QString::fromUtf8(a.rootDir);
    d.includeDirs = toQStringList(a.includeDirs);
    d.asmIncludeDirs = toQStringList(a.asmIncludeDirs);
    d.libDirs = toQStringList(a.libDirs);
    d.autodocDirs = toQStringList(a.autodocDirs);
    d.autodocFiles = toQStringList(a.autodocFiles);
    d.sfdDirs = toQStringList(a.sfdDirs);
    d.suggestedLibs = toQStringList(a.suggestedLibs);
    d.userDefined = a.userDefined != 0;
    return d;
}

QJsonObject SdkDescriptor::toJson() const
{
    AideSdkDescriptor a;
    sdkDescriptorToAide(*this, &a);
    char *json = aide_sdk_descriptor_to_json(&a);
    aide_sdk_descriptor_free(&a);

    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    free(json);
    return doc.object();
}

SdkDescriptor SdkDescriptor::fromJson(const QJsonObject &obj)
{
    const QByteArray json = QJsonDocument(obj).toJson();
    AideSdkDescriptor a;
    aide_sdk_descriptor_init(&a);
    aide_sdk_descriptor_from_json(&a, json.constData());
    SdkDescriptor d = sdkDescriptorFromAide(a);
    aide_sdk_descriptor_free(&a);
    return d;
}
