#include "index/IncludeGraph.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// Covers the exact resolution path MainWindow::tryOpenIncludeOnLine uses for
// F3/Cmd+Click on an #include/include line: quoted-relative-to-includer,
// angle-bracket-via-search-path, and the "not found" (e.g. SDK header) case.
class TstIncludeGraph : public QObject {
    Q_OBJECT
private slots:
    void resolvesQuotedRelativeToIncludingFile();
    void resolvesAngleBracketViaSearchPath();
    void returnsEmptyWhenNotFound();

private:
    QTemporaryDir m_dir;
};

void TstIncludeGraph::resolvesQuotedRelativeToIncludingFile()
{
    QDir(m_dir.path()).mkpath("src");
    const QString headerPath = m_dir.path() + "/src/utils.h";
    QFile header(headerPath);
    QVERIFY(header.open(QFile::WriteOnly));
    header.close();

    // #include "utils.h" from src/main.c -- resolved relative to main.c's directory.
    const QString resolved = IncludeGraph::resolveInclude(
        QStringLiteral("utils.h"), /*quoted=*/true, m_dir.path() + "/src", {});
    QCOMPARE(QFileInfo(resolved).canonicalFilePath(), QFileInfo(headerPath).canonicalFilePath());
}

void TstIncludeGraph::resolvesAngleBracketViaSearchPath()
{
    QDir(m_dir.path()).mkpath("include");
    const QString headerPath = m_dir.path() + "/include/hw.i";
    QFile header(headerPath);
    QVERIFY(header.open(QFile::WriteOnly));
    header.close();

    // include "hw.i" from an asm file elsewhere, resolved via the project's include path.
    const QString resolved = IncludeGraph::resolveInclude(
        QStringLiteral("hw.i"), /*quoted=*/false, m_dir.path() + "/src",
        { m_dir.path() + "/include" });
    QCOMPARE(QFileInfo(resolved).canonicalFilePath(), QFileInfo(headerPath).canonicalFilePath());
}

void TstIncludeGraph::returnsEmptyWhenNotFound()
{
    // e.g. <exec/types.h> -- an SDK header, deliberately out of scope (plan
    // section 8: SDK headers aren't part of the project-local include graph).
    const QString resolved = IncludeGraph::resolveInclude(
        QStringLiteral("exec/types.h"), /*quoted=*/false, m_dir.path() + "/src", {});
    QVERIFY(resolved.isEmpty());
}

QTEST_APPLESS_MAIN(TstIncludeGraph)
#include "tst_includegraph.moc"
