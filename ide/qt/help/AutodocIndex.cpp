#include "AutodocIndex.h"
#include "sdk/SdkDescriptorAide.h"

#include <QStandardPaths>

#include <cstdlib>
#include <vector>

// Thin adapter over the portable core/ implementation (core/src/aide_autodoc.c,
// shared with the Amiga/MUI build).

namespace {
QString cacheDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/autodoc-cache");
}
}

AutodocIndex::AutodocIndex()
{
    const QByteArray dirUtf8 = cacheDirPath().toUtf8();
    aide_autodocindex_init(&m_index, dirUtf8.constData());
}

AutodocIndex::~AutodocIndex()
{
    aide_autodocindex_free(&m_index);
}

void AutodocIndex::reload(const QList<SdkDescriptor> &activeSdks)
{
    std::vector<AideSdkDescriptor> aideSdks;
    aideSdks.reserve(size_t(activeSdks.size()));
    for (const SdkDescriptor &d : activeSdks) {
        AideSdkDescriptor a;
        sdkDescriptorToAide(d, &a);
        aideSdks.push_back(a);
    }

    aide_autodocindex_reload(&m_index, aideSdks.empty() ? nullptr : aideSdks.data(), aideSdks.size(), nullptr, nullptr);

    for (AideSdkDescriptor &a : aideSdks)
        aide_sdk_descriptor_free(&a);
}

QList<AutodocEntry> AutodocIndex::find(const QString &functionName) const
{
    const QByteArray nameUtf8 = functionName.toUtf8();
    AideAutodocEntry *entries = nullptr;
    size_t count = 0;
    aide_autodocindex_find(&m_index, nameUtf8.constData(), &entries, &count);

    QList<AutodocEntry> result;
    for (size_t i = 0; i < count; ++i) {
        AutodocEntry e;
        e.function = QString::fromUtf8(entries[i].function);
        e.library = QString::fromUtf8(entries[i].library);
        e.docFile = QString::fromUtf8(entries[i].docFile);
        e.offset = entries[i].offset;
        e.length = entries[i].length;
        result << e;
        aide_autodocentry_free(&entries[i]);
    }
    free(entries);
    return result;
}

QStringList AutodocIndex::findWildcardNames(const QString &pattern) const
{
    const QByteArray patternUtf8 = pattern.toUtf8();
    AideStrList names;
    aide_strlist_init(&names);
    aide_autodocindex_find_wildcard_names(&m_index, patternUtf8.constData(), &names);

    QStringList result;
    result.reserve(int(names.count));
    for (size_t i = 0; i < names.count; ++i)
        result << QString::fromUtf8(names.items[i]);
    aide_strlist_free(&names);
    return result;
}

QString AutodocIndex::readEntryText(const AutodocEntry &entry) const
{
    AideAutodocEntry a;
    const QByteArray funcUtf8 = entry.function.toUtf8();
    const QByteArray libUtf8 = entry.library.toUtf8();
    const QByteArray fileUtf8 = entry.docFile.toUtf8();
    a.function = const_cast<char *>(funcUtf8.constData());
    a.library = const_cast<char *>(libUtf8.constData());
    a.docFile = const_cast<char *>(fileUtf8.constData());
    a.offset = long(entry.offset);
    a.length = long(entry.length);

    char *text = aide_autodocindex_read_entry_text(&a);
    const QString result = QString::fromUtf8(text);
    free(text);
    return result;
}
