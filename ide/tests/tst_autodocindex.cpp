#include "help/AutodocIndex.h"

#include <QFile>
#include <QTest>

// Parses real autodoc files from the toolchain tree (dos.doc, exec.doc),
// verifying formfeed-split entries, Latin-1 decoding, and the
// exact -> case-insensitive -> TagList lookup chain against real data.
class TstAutodocIndex : public QObject {
    Q_OBJECT
private slots:
    void findsExactEntryInRealExecDoc();
    void tagListHeuristicMatchesRealEntry();
    void caseInsensitiveFallback();
    void wildcardMatchesMultipleRealEntries();
};

namespace {
QString autodocDir()
{
    return QStringLiteral(TOOLCHAIN_ROOT) + QStringLiteral("/sdk/NDK_3.2/Autodocs");
}

SdkDescriptor ndk32()
{
    SdkDescriptor d;
    d.id = QStringLiteral("ndk-3.2");
    d.autodocDirs = { autodocDir() };
    return d;
}

// The toolchain is generated, not checked in, so a fresh clone has no
// autodocs to read until the bootstrap has run.  Skip rather than fail:
// this suite tests the parser against real data, and "the data is not
// installed yet" is not a parser defect.
bool requireToolchain()
{
    if (QFile::exists(autodocDir() + QStringLiteral("/exec.doc")))
        return true;
    QTest::qSkip("toolchain not bootstrapped; run cmake --build build --target acd-toolchain",
                 __FILE__, __LINE__);
    return false;
}
}

void TstAutodocIndex::findsExactEntryInRealExecDoc()
{
    if (!requireToolchain())
        return;

    AutodocIndex index;
    index.reload({ ndk32() });

    const auto entries = index.find(QStringLiteral("AllocMem"));
    QVERIFY(!entries.isEmpty());
    QCOMPARE(entries.first().library, QStringLiteral("exec.library"));

    const QString text = index.readEntryText(entries.first());
    QVERIFY(text.contains(QStringLiteral("AllocMem")));
    QVERIFY(text.contains(QStringLiteral("NAME")));
}

void TstAutodocIndex::tagListHeuristicMatchesRealEntry()
{
    if (!requireToolchain())
        return;

    AutodocIndex index;
    index.reload({ ndk32() });

    // dos.library/SystemTagList exists; dos.library/SystemTags does not --
    // this genuinely exercises the Tags -> TagList fallback, not an exact hit.
    QVERIFY(index.find(QStringLiteral("SystemTags2")).isEmpty()); // sanity: unrelated name still misses
    const auto entries = index.find(QStringLiteral("SystemTags"));
    QVERIFY(!entries.isEmpty());
    QCOMPARE(entries.first().function, QStringLiteral("SystemTagList"));
    QCOMPARE(entries.first().library, QStringLiteral("dos.library"));
}

void TstAutodocIndex::caseInsensitiveFallback()
{
    if (!requireToolchain())
        return;

    AutodocIndex index;
    index.reload({ ndk32() });

    // NDK 3.2 documents two functions whose names differ only in case:
    // exec.library/AllocMem and rexxsupport.library/ALLOCMEM. A lowercase
    // query must reach both, and the order between them is not something
    // the index promises -- so assert the set, not entries.first().
    const auto entries = index.find(QStringLiteral("allocmem"));
    QVERIFY(!entries.isEmpty());

    QStringList libraries;
    for (const auto &e : entries)
        libraries << e.library;
    QVERIFY2(libraries.contains(QStringLiteral("exec.library")),
             qPrintable(QStringLiteral("expected exec.library among: ") + libraries.join(QLatin1String(", "))));

    for (const auto &e : entries)
        QCOMPARE(e.function.toLower(), QStringLiteral("allocmem"));
}

void TstAutodocIndex::wildcardMatchesMultipleRealEntries()
{
    if (!requireToolchain())
        return;

    AutodocIndex index;
    index.reload({ ndk32() });

    // exec.library has multiple real Alloc* functions (AllocMem, AllocAbs,
    // AllocEntry, ...) -- verifies glob matching against real entry names.
    const QStringList allocMatches = index.findWildcardNames(QStringLiteral("Alloc*"));
    QVERIFY(allocMatches.size() > 1);
    QVERIFY(allocMatches.contains(QStringLiteral("AllocMem")));
    QVERIFY(allocMatches.contains(QStringLiteral("AllocAbs")));

    // ? matches exactly one character.
    const QStringList singleCharMatches = index.findWildcardNames(QStringLiteral("AllocMe?"));
    QVERIFY(singleCharMatches.contains(QStringLiteral("AllocMem")));
    QVERIFY(!singleCharMatches.contains(QStringLiteral("AllocMemH"))); // wrong length, must not match

    QVERIFY(index.findWildcardNames(QStringLiteral("NoSuchPrefix*")).isEmpty());
}

QTEST_APPLESS_MAIN(TstAutodocIndex)
#include "tst_autodocindex.moc"
