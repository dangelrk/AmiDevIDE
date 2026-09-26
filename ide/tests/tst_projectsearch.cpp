#include "search/ProjectSearch.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

// Covers the scan engine behind "Find in Project..." and "Find All
// References": whole-word vs. substring matching, case sensitivity, user
// regex patterns, matches spanning multiple files, and column reporting.
class TstProjectSearch : public QObject {
    Q_OBJECT
private slots:
    void substringMatchesPartialWord();
    void wholeWordExcludesPartialMatches();
    void caseInsensitiveByDefault();
    void caseSensitiveWhenRequested();
    void regexPatternMatches();
    void reportsCorrectColumn();
    void searchesAcrossMultipleFiles();
    void skipsUnreadableFiles();

private:
    QString writeFile(const QString &name, const QString &contents);

    QTemporaryDir m_dir;
};

QString TstProjectSearch::writeFile(const QString &name, const QString &contents)
{
    const QString path = m_dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return QString();
    QTextStream out(&f);
    out << contents;
    return path;
}

void TstProjectSearch::substringMatchesPartialWord()
{
    const QString path = writeFile(QStringLiteral("a.c"), QStringLiteral("int AllocMemory(void);\n"));
    const auto matches = ProjectSearch::search({ path }, QStringLiteral("Alloc"),
                                               /*caseSensitive=*/true, /*wholeWord=*/false, /*regex=*/false);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().line, 1);
    QCOMPARE(matches.first().column, 4);
}

void TstProjectSearch::wholeWordExcludesPartialMatches()
{
    const QString path = writeFile(QStringLiteral("b.c"),
        QStringLiteral("int AllocMemory(void);\nAlloc(1);\n"));
    const auto matches = ProjectSearch::search({ path }, QStringLiteral("Alloc"),
                                               /*caseSensitive=*/true, /*wholeWord=*/true, /*regex=*/false);
    QCOMPARE(matches.size(), 1); // only the standalone "Alloc(1);" line, not "AllocMemory"
    QCOMPARE(matches.first().line, 2);
}

void TstProjectSearch::caseInsensitiveByDefault()
{
    const QString path = writeFile(QStringLiteral("c.c"), QStringLiteral("ALLOCMEM(x);\n"));
    const auto matches = ProjectSearch::search({ path }, QStringLiteral("allocmem"),
                                               /*caseSensitive=*/false, /*wholeWord=*/false, /*regex=*/false);
    QCOMPARE(matches.size(), 1);
}

void TstProjectSearch::caseSensitiveWhenRequested()
{
    const QString path = writeFile(QStringLiteral("d.c"), QStringLiteral("ALLOCMEM(x);\n"));
    const auto matches = ProjectSearch::search({ path }, QStringLiteral("allocmem"),
                                               /*caseSensitive=*/true, /*wholeWord=*/false, /*regex=*/false);
    QVERIFY(matches.isEmpty());
}

void TstProjectSearch::regexPatternMatches()
{
    const QString path = writeFile(QStringLiteral("e.c"),
        QStringLiteral("AllocMem(1);\nAllocAbs(2);\nFreeMem(3);\n"));
    const auto matches = ProjectSearch::search({ path }, QStringLiteral("Alloc\\w+"),
                                               /*caseSensitive=*/true, /*wholeWord=*/false, /*regex=*/true);
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches[0].line, 1);
    QCOMPARE(matches[1].line, 2);
}

void TstProjectSearch::reportsCorrectColumn()
{
    const QString path = writeFile(QStringLiteral("f.c"), QStringLiteral("    return AllocMem(1);\n"));
    const auto matches = ProjectSearch::search({ path }, QStringLiteral("AllocMem"),
                                               /*caseSensitive=*/true, /*wholeWord=*/false, /*regex=*/false);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().column, 11); // 0-based offset of "AllocMem" in the line
    QCOMPARE(matches.first().lineText, QStringLiteral("    return AllocMem(1);"));
}

void TstProjectSearch::searchesAcrossMultipleFiles()
{
    const QString path1 = writeFile(QStringLiteral("g1.c"), QStringLiteral("AllocMem(1);\n"));
    const QString path2 = writeFile(QStringLiteral("g2.c"), QStringLiteral("x = AllocMem(2);\n"));
    const auto matches = ProjectSearch::search({ path1, path2 }, QStringLiteral("AllocMem"),
                                               /*caseSensitive=*/true, /*wholeWord=*/false, /*regex=*/false);
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches[0].file, path1);
    QCOMPARE(matches[1].file, path2);
}

void TstProjectSearch::skipsUnreadableFiles()
{
    const QString missing = m_dir.path() + QStringLiteral("/does-not-exist.c");
    const auto matches = ProjectSearch::search({ missing }, QStringLiteral("anything"),
                                               /*caseSensitive=*/true, /*wholeWord=*/false, /*regex=*/false);
    QVERIFY(matches.isEmpty()); // must not crash, just skip
}

QTEST_APPLESS_MAIN(TstProjectSearch)
#include "tst_projectsearch.moc"
