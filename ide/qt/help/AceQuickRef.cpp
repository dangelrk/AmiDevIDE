#include "AceQuickRef.h"

#include <QFile>
#include <QTextStream>

void AceQuickRef::load(const QString &sdkDir)
{
    const QString path = sdkDir + QStringLiteral("/quickref.txt");
    if (path == m_loadedFrom)
        return;

    m_loadedFrom = path;
    m_entries.clear();

    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0)
            continue;
        const QString word = line.left(space);
        const QString description = line.mid(space + 1).trimmed();
        if (!description.isEmpty())
            m_entries.insert(word.toUpper(), description);
    }
}

QString AceQuickRef::lookup(const QString &word) const
{
    if (word.isEmpty())
        return QString();

    const QString key = word.toUpper();
    auto it = m_entries.constFind(key);
    if (it != m_entries.constEnd())
        return it.value();

    // Functions are listed with their opening parenthesis ("ABS("), but the
    // word under the cursor is just the identifier.
    it = m_entries.constFind(key + QLatin1Char('('));
    if (it != m_entries.constEnd())
        return it.value();

    return QString();
}
