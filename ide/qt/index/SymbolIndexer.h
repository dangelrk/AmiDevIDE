#pragma once

#include "project/Project.h"

#include "Symbol.h"

#include <QObject>
#include <QString>
#include <QStringList>

// Runs on its own QThread; talks to the rest of the app exclusively through
// queued signals/slots (no shared state, no locking -- plan section 8).
class SymbolIndexer : public QObject {
    Q_OBJECT
public:
    explicit SymbolIndexer(QObject *parent = nullptr);

public slots:
    // Must be called (queued) before any indexFile() calls for a project.
    void setProjectContext(const QString &projectRoot, const QStringList &includePaths,
                           ProjectLanguage language = ProjectLanguage::C);

    // Reads the file from disk, parses it (by extension), resolves
    // #include/incdir targets that point at other project-local files, and
    // emits fileIndexed.
    void indexFile(const QString &absolutePath);

signals:
    void fileIndexed(const QString &path, QList<Symbol> symbols, QStringList resolvedIncludes);

private:
    QString m_projectRoot;
    QStringList m_includePaths;
    ProjectLanguage m_language = ProjectLanguage::C;
};
