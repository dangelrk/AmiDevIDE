#include "app/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

// Settings outlive the tree they describe. Moving the toolchain -- or coming
// from the old ~/Dev/amiga layout -- leaves QSettings pointing at a directory
// that is gone, and the first sign of it is `make` failing with
// "No such file or directory" for vc, several steps removed from the cause.
class TstAppSettings : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();

    void recognisesAToolchainByItsCompiler();
    void rejectsADirectoryWithoutOne();
    void healsAStoredRootThatNoLongerExists();
    void keepsAStoredRootThatStillWorks();
    void clearsPathsDerivedFromTheOldRoot();
    void keepsPathsThatStillExist();
    void reportsNothingWhenThereIsNothingToDo();

private:
    QTemporaryDir m_dir;
    QString makeToolchain(const QString &name);
};

void TstAppSettings::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // Never touch the developer's real preferences: a distinct org/app name
    // gives this suite its own store, and init() clears it each time.
    QCoreApplication::setOrganizationName(QStringLiteral("AmigaIDE-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_appsettings"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

void TstAppSettings::init()
{
    QSettings().clear();
}

// A toolchain is recognised by bin/vc, so a plausible-looking but empty
// directory is correctly rejected.
QString TstAppSettings::makeToolchain(const QString &name)
{
    const QString root = m_dir.filePath(name);
    QDir().mkpath(root + QStringLiteral("/bin"));
    QFile vc(root + QStringLiteral("/bin/vc"));
    vc.open(QIODevice::WriteOnly);
    vc.write("#!/bin/sh\n");
    vc.close();
    return root;
}

void TstAppSettings::recognisesAToolchainByItsCompiler()
{
    QVERIFY(AppSettings::looksLikeToolchain(makeToolchain(QStringLiteral("good"))));
}

void TstAppSettings::rejectsADirectoryWithoutOne()
{
    const QString empty = m_dir.filePath(QStringLiteral("empty"));
    QDir().mkpath(empty + QStringLiteral("/bin"));
    QVERIFY(!AppSettings::looksLikeToolchain(empty));
    QVERIFY(!AppSettings::looksLikeToolchain(m_dir.filePath(QStringLiteral("absent"))));
    QVERIFY(!AppSettings::looksLikeToolchain(QString()));
}

void TstAppSettings::healsAStoredRootThatNoLongerExists()
{
    const QString gone = m_dir.filePath(QStringLiteral("removed-toolchain"));
    QSettings().setValue(QStringLiteral("toolchain/root"), gone);

    const QStringList changes = AppSettings::instance().migrateStaleSettings();

    // The test binary does not live in a toolchain, so detection finds none
    // and the stored value is reported rather than silently replaced.
    QCOMPARE(changes.size(), 1);
    QVERIFY2(changes.first().contains(gone), qPrintable(changes.first()));
    QVERIFY(!AppSettings::looksLikeToolchain(
        QSettings().value(QStringLiteral("toolchain/root")).toString()));
}

void TstAppSettings::keepsAStoredRootThatStillWorks()
{
    const QString root = makeToolchain(QStringLiteral("live"));
    QSettings().setValue(QStringLiteral("toolchain/root"), root);

    const QStringList changes = AppSettings::instance().migrateStaleSettings();

    QVERIFY2(changes.isEmpty(), qPrintable(changes.join(QLatin1String("; "))));
    QCOMPARE(AppSettings::instance().toolchainRoot(), root);
}

void TstAppSettings::clearsPathsDerivedFromTheOldRoot()
{
    const QString gone = m_dir.filePath(QStringLiteral("old"));
    QSettings s;
    s.setValue(QStringLiteral("toolchain/root"), gone);
    s.setValue(QStringLiteral("run/sharedDir"), gone + QStringLiteral("/shared"));
    // The retired per-OS emulator layout, which is now one generated file.
    s.setValue(QStringLiteral("run/fsUaeConfig"),
               gone + QStringLiteral("/fs-uae/mac/development.fs-uae"));
    s.setValue(QStringLiteral("run/fsUaeBinary"), gone + QStringLiteral("/fs-uae"));

    const QStringList changes = AppSettings::instance().migrateStaleSettings();

    QCOMPARE(changes.size(), 4);
    QVERIFY(!s.contains(QStringLiteral("run/sharedDir")));
    QVERIFY(!s.contains(QStringLiteral("run/fsUaeConfig")));
    QVERIFY(!s.contains(QStringLiteral("run/fsUaeBinary")));
}

void TstAppSettings::keepsPathsThatStillExist()
{
    const QString root = makeToolchain(QStringLiteral("current"));
    const QString shared = root + QStringLiteral("/shared");
    QDir().mkpath(shared);
    QFile cfg(root + QStringLiteral("/machine.fs-uae"));
    cfg.open(QIODevice::WriteOnly);
    cfg.close();

    QSettings s;
    s.setValue(QStringLiteral("toolchain/root"), root);
    s.setValue(QStringLiteral("run/sharedDir"), shared);
    s.setValue(QStringLiteral("run/fsUaeConfig"), cfg.fileName());

    QVERIFY(AppSettings::instance().migrateStaleSettings().isEmpty());
    QCOMPARE(s.value(QStringLiteral("run/sharedDir")).toString(), shared);
    QCOMPARE(s.value(QStringLiteral("run/fsUaeConfig")).toString(), cfg.fileName());
}

void TstAppSettings::reportsNothingWhenThereIsNothingToDo()
{
    // A fresh install has no stored paths at all; migration must stay quiet
    // rather than announce defaults it never changed.
    QVERIFY(AppSettings::instance().migrateStaleSettings().isEmpty());
}

QTEST_MAIN(TstAppSettings)
#include "tst_appsettings.moc"
