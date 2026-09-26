#include "index/SfdIndex.h"

#include <QFile>
#include <QTest>

// Parses the real exec_lib.sfd shipped in the toolchain tree, including its
// multi-line MakeLibrary entry (continuation lines are tab-indented) --
// verifies the parser against real data, not just hand-crafted fixtures.
class TstSfdIndex : public QObject {
private:
    Q_OBJECT
private slots:
    void parsesRealExecLibSfd();
};

void TstSfdIndex::parsesRealExecLibSfd()
{
    const QString sfdDir = QStringLiteral(TOOLCHAIN_ROOT) + QStringLiteral("/sdk/NDK_3.2/SFD");
    // Generated tree: skip until the bootstrap has installed the NDK.
    if (!QFile::exists(sfdDir + QStringLiteral("/exec_lib.sfd")))
        QSKIP("toolchain not bootstrapped; run cmake --build build --target acd-toolchain");

    SfdIndex index;
    index.reload({ sfdDir });

    const SfdFunction *supervisor = index.find(QStringLiteral("Supervisor"));
    QVERIFY(supervisor != nullptr);
    QCOMPARE(supervisor->library, QStringLiteral("exec.library"));
    QCOMPARE(supervisor->registers, QStringLiteral("a5"));
    QVERIFY(supervisor->rawSignature.contains(QStringLiteral("Supervisor")));

    // Multi-line entry (continuation line is tab-indented in the real file).
    const SfdFunction *makeLibrary = index.find(QStringLiteral("MakeLibrary"));
    QVERIFY(makeLibrary != nullptr);
    QCOMPARE(makeLibrary->library, QStringLiteral("exec.library"));
    QCOMPARE(makeLibrary->registers, QStringLiteral("a0,a1,a2,d0,d1"));
    QVERIFY(makeLibrary->rawSignature.contains(QStringLiteral("libInit")));
    QVERIFY(makeLibrary->rawSignature.contains(QStringLiteral("dataSize")));

    const SfdFunction *allocMem = index.find(QStringLiteral("AllocMem"));
    QVERIFY(allocMem != nullptr);
    QCOMPARE(allocMem->registers, QStringLiteral("d0,d1"));

    QVERIFY(index.find(QStringLiteral("NoSuchFunctionAtAll")) == nullptr);
}

QTEST_APPLESS_MAIN(TstSfdIndex)
#include "tst_sfdindex.moc"
