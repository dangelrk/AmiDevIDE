#include "SymbolDatabase.h"

SymbolDatabase::SymbolDatabase(QObject *parent)
    : QObject(parent)
{
}

void SymbolDatabase::setFileSymbols(const QString &file, const QList<Symbol> &symbols)
{
    m_byFile[file] = symbols;
    emit databaseChanged();
}

void SymbolDatabase::removeFile(const QString &file)
{
    if (m_byFile.remove(file) > 0)
        emit databaseChanged();
}

QList<Symbol> SymbolDatabase::findByName(const QString &name) const
{
    QList<Symbol> result;
    for (auto it = m_byFile.constBegin(); it != m_byFile.constEnd(); ++it) {
        for (const Symbol &s : it.value()) {
            if (s.name == name)
                result << s;
        }
    }
    return result;
}

QList<Symbol> SymbolDatabase::symbolsInFile(const QString &file) const
{
    return m_byFile.value(file);
}

QList<Symbol> SymbolDatabase::allSymbols() const
{
    QList<Symbol> result;
    for (auto it = m_byFile.constBegin(); it != m_byFile.constEnd(); ++it)
        result << it.value();
    return result;
}

QList<Symbol> SymbolDatabase::membersOfStruct(const QString &structName) const
{
    QList<Symbol> result;
    for (auto it = m_byFile.constBegin(); it != m_byFile.constEnd(); ++it) {
        for (const Symbol &s : it.value()) {
            if (s.kind == SymbolKind::StructMember && s.scope == structName)
                result << s;
        }
    }
    return result;
}
