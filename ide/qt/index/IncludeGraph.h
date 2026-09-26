#pragma once

#include "aide/aide_includegraph.h"

#include <QString>
#include <QStringList>

// Tracks direct #include / asm include-incdir edges, resolved to absolute
// paths, project-local files only (SDK headers are out of scope: they never
// change, and are deliberately not symbol-indexed either -- plan section 8).
// Feeds MakefileGenerator's per-object header prerequisites and go-to-
// definition's "search included files" step.
//
// Thin adapter over the portable core/ implementation
// (core/src/aide_includegraph.c, shared with the Amiga/MUI build); this
// class just owns the
// AideIncludeGraph's lifetime and converts QString/QStringList at the edges.
class IncludeGraph {
public:
    IncludeGraph();
    ~IncludeGraph();
    IncludeGraph(const IncludeGraph &) = delete;
    IncludeGraph &operator=(const IncludeGraph &) = delete;

    void setDirectIncludes(const QString &file, const QStringList &resolvedAbsoluteIncludes);
    void removeFile(const QString &file);

    QStringList directIncludes(const QString &file) const;
    QStringList transitiveIncludes(const QString &file) const;

    // Resolves a raw #include/incdir directive to an absolute path, if it
    // refers to an existing project-local file. Returns QString() otherwise
    // (e.g. SDK headers), which the caller should treat as "not tracked".
    static QString resolveInclude(const QString &directive, bool quoted,
                                   const QString &includingFileDir,
                                   const QStringList &searchPaths);

private:
    AideIncludeGraph m_graph;
};
