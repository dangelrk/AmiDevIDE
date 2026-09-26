#pragma once

#include "Symbol.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

// Lives in the UI thread; updated only from SymbolIndexer's fileIndexed
// signal (queued connection), so no locking is needed (plan section 8).
class SymbolDatabase : public QObject {
    Q_OBJECT
public:
    explicit SymbolDatabase(QObject *parent = nullptr);

    void setFileSymbols(const QString &file, const QList<Symbol> &symbols);
    void removeFile(const QString &file);

    QList<Symbol> findByName(const QString &name) const;
    QList<Symbol> symbolsInFile(const QString &file) const;
    QList<Symbol> allSymbols() const;

    // All StructMember symbols whose scope == structName (the struct/union's
    // own tag, or a typedef name for an anonymous typedef'd struct), in
    // declaration order isn't guaranteed across files -- callers that care
    // about order should sort by offset.
    QList<Symbol> membersOfStruct(const QString &structName) const;

signals:
    void databaseChanged();

private:
    QHash<QString, QList<Symbol>> m_byFile;
};
