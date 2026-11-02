#include "app/AppSettings.h"
#include "vcs/VcsRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// VcsRunner against a real amivcs (AIDE_TEST_AMIVCS; skipped without):
// tool check, jobs in the project directory, write lane exclusive, reads
// queued behind a write, results parsed.
class TstVcsRunner : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void missingTool();
    void roundTrip();

private:
    VcsResultPtr runAndWait(VcsRunner &r, AideVcsOp op, const QStringList &items = {},
                            const QString &msg = {}, bool flag = false);
    QString m_amivcs;
};

void TstVcsRunner::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("AmigaIDE-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_vcsrunner"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings().clear();
    m_amivcs = QString::fromUtf8(qgetenv("AIDE_TEST_AMIVCS"));
}

VcsResultPtr TstVcsRunner::runAndWait(VcsRunner &r, AideVcsOp op, const QStringList &items,
                                      const QString &msg, bool flag)
{
    QSignalSpy spy(&r, &VcsRunner::finished);
    if (!r.run(op, items, msg, flag)) return {};
    for (int i = 0; i < 100 && spy.isEmpty(); ++i) spy.wait(100);
    if (spy.isEmpty()) return {};
    return spy.takeFirst().at(1).value<VcsResultPtr>();
}

void TstVcsRunner::missingTool()
{
    AppSettings::instance().setAmivcsProgram(QStringLiteral("/nonexistent/amivcs"));
    VcsRunner r;
    QSignalSpy spy(&r, &VcsRunner::toolChecked);
    r.checkTool();
    QCOMPARE(spy.size(), 1);
    QVERIFY(!spy.first().at(0).toBool());
    QVERIFY(!r.toolAvailable());
}

void TstVcsRunner::roundTrip()
{
    if (m_amivcs.isEmpty() || !QFile::exists(m_amivcs))
        QSKIP("AIDE_TEST_AMIVCS not set");
    AppSettings::instance().setAmivcsProgram(m_amivcs);
    QTemporaryDir dir;
    QFile src(dir.filePath(QStringLiteral("main.c")));
    QVERIFY(src.open(QFile::WriteOnly));
    src.write("int main(void){return 0;}\n");
    src.close();

    VcsRunner r;
    r.setProjectRoot(dir.path());
    QSignalSpy checked(&r, &VcsRunner::toolChecked);
    r.checkTool();
    QVERIFY(checked.wait(5000));
    QVERIFY(r.toolAvailable());

    VcsResultPtr res = runAndWait(r, AIDE_VCS_INIT);
    QVERIFY(res && res->ok());
    res = runAndWait(r, AIDE_VCS_ADD, { QStringLiteral("main.c") });
    QVERIFY(res && res->ok());
    QCOMPARE(aide_vcs_file_state(&res->r, "main.c", 0), 'S');   // status came along

    // a second writing job is refused while one runs; a read waits for it
    QSignalSpy done(&r, &VcsRunner::finished);
    QVERIFY(r.run(AIDE_VCS_COMMIT, {}, QStringLiteral("Erster Stand"), false));
    QVERIFY(r.writing());
    QVERIFY(!r.run(AIDE_VCS_BRANCH_NEW, { QStringLiteral("dev") }));
    QVERIFY(r.run(AIDE_VCS_LOG));
    for (int i = 0; i < 100 && done.size() < 2; ++i) done.wait(100);
    QCOMPARE(done.size(), 2);
    QCOMPARE(done.at(0).at(0).toInt(), int(AIDE_VCS_COMMIT));   // the write first
    QCOMPARE(done.at(1).at(0).toInt(), int(AIDE_VCS_LOG));
    const VcsResultPtr log = done.at(1).at(1).value<VcsResultPtr>();
    QCOMPARE(log->r.commitCount, size_t(1));

    res = runAndWait(r, AIDE_VCS_SWITCH, { QStringLiteral("nixda") });
    QVERIFY(res && !res->ok());
    QVERIFY(res->error().contains(QStringLiteral("no such branch")));
}

QTEST_MAIN(TstVcsRunner)
#include "tst_vcsrunner.moc"
