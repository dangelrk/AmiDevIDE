#pragma once

#include "aide/aide_autodoc.h"
#include "sdk/SdkDescriptor.h"

#include <QList>
#include <QString>
#include <QStringList>

// One autodoc entry: exec.library/AbortIO, etc. Text is read lazily as a
// byte/char slice of its source .doc file (Latin-1, entries separated by \f
// -- plan section 9). Byte offset == char offset since Latin-1 is single-byte.
struct AutodocEntry {
    QString function;
    QString library;
    QString docFile;
    qint64 offset = 0;
    qint64 length = 0;
};

// Scans the autodocDirs/autodocFiles of all active SDKs. Cached to a JSON
// file per SDK id under AppDataLocation, keyed on (path, size, mtime), so an
// SDK selection change only rescans the segments that actually changed.
//
// Thin adapter over the portable core/ implementation
// (core/src/aide_autodoc.c, shared with the Amiga/MUI build); this class
// just owns the
// AideAutodocIndex's lifetime and converts QString/QList at the edges.
class AutodocIndex {
public:
    AutodocIndex();
    ~AutodocIndex();
    AutodocIndex(const AutodocIndex &) = delete;
    AutodocIndex &operator=(const AutodocIndex &) = delete;

    void reload(const QList<SdkDescriptor> &activeSdks);

    // exact -> case-insensitive -> TagList heuristic (OpenWindowTags ->
    // OpenWindowTagList), per plan section 9.
    QList<AutodocEntry> find(const QString &functionName) const;

    // All indexed function names matching a glob pattern (* and ?), for the
    // Help dock's wildcard search.
    QStringList findWildcardNames(const QString &pattern) const;

    QString readEntryText(const AutodocEntry &entry) const;

private:
    AideAutodocIndex m_index;
};
