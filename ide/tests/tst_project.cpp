#include "project/Project.h"
#include "aide/aide_project.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <clocale>

// Project.cpp is now a thin adapter over core/src/aide_project.c (JSON via
// parson). No prior test existed
// for this class; this one guards the .aproj round trip and the
// forward-compat "unknown top-level keys survive save" behavior across the
// aide_project.c rewrite.
class TstProject : public QObject {
    Q_OBJECT
private slots:
    void minimalCDefaults();
    void minimalAsmDefaults();
    void saveLoadRoundTrip();
    void unknownKeysPreserved();
    void noSdkSelectedRoundTrips();
    void languageAndOutputKindRoundTrip();
    void legacyProjectStaysC();
    void editorStateInUserFile();
    void legacyUserFile();
    void amigaNames();
    void loadsUnderGermanLocale();
};

void TstProject::minimalCDefaults()
{
    const Project p = Project::createMinimalC(QStringLiteral("demo"));
    QCOMPARE(p.name, QStringLiteral("demo"));
    QCOMPARE(p.output.binary, QStringLiteral("demo"));
    QCOMPARE(p.output.targetDir, QStringLiteral("bin"));
    QCOMPARE(p.output.objDir, QStringLiteral("build/obj"));
    QCOMPARE(p.sources.size(), 1);
    QCOMPARE(p.sources.first().path, QStringLiteral("src/main.c"));
    QCOMPARE(p.linker.startup, LinkerConfig::Startup::Std);
    QCOMPARE(p.linker.libraries, QStringList{ QStringLiteral("vc") });
    QCOMPARE(p.compiler.cpu, QStringLiteral("68000"));
    QCOMPARE(p.compiler.fpu, QStringLiteral("none"));
    QCOMPARE(p.compiler.optLevel, 2);
    QVERIFY(p.compiler.kick1hunks);
    QVERIFY(p.compiler.asmNoSym);
    QCOMPARE(p.sdks.selected, QStringList{ QStringLiteral("ndk-3.2") });
    QCOMPARE(p.program.stackSize, 8192);
    QCOMPARE(p.editor.activeFile, QStringLiteral("src/main.c"));
}

void TstProject::minimalAsmDefaults()
{
    const Project p = Project::createMinimalAsm(QStringLiteral("demo"));
    QCOMPARE(p.sources.first().path, QStringLiteral("src/main.s"));
    QCOMPARE(p.linker.startup, LinkerConfig::Startup::MinStart);
}

void TstProject::saveLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("demo.aproj"));

    Project p = Project::createMinimalC(QStringLiteral("demo"));
    p.includePaths << QStringLiteral("include");
    p.defines << QStringLiteral("VERSION=1");
    p.compiler.cpu = QStringLiteral("68020");
    p.compiler.smallData = true;
    p.compiler.debugSymbols = true;
    p.linker.libraries = { QStringLiteral("vc"), QStringLiteral("amiga") };
    p.linker.strip = true;
    p.sdks.selected << QStringLiteral("mui-3.8")
                    << QStringLiteral("roadshow") << QStringLiteral("amissl");
    p.program.type = ProgramConfig::Type::Wb;
    p.program.stackSize = 65536;
    QVERIFY(p.save(path));

    Project loaded;
    QVERIFY(loaded.load(path));

    QCOMPARE(loaded.name, p.name);
    QCOMPARE(loaded.includePaths, p.includePaths);
    QCOMPARE(loaded.defines, p.defines);
    QCOMPARE(loaded.compiler.cpu, QStringLiteral("68020"));
    QVERIFY(loaded.compiler.smallData);
    QVERIFY(loaded.compiler.debugSymbols);
    QCOMPARE(loaded.linker.libraries, p.linker.libraries);
    QVERIFY(loaded.linker.strip);
    QCOMPARE(loaded.sdks.selected, p.sdks.selected);
    QCOMPARE(loaded.program.type, ProgramConfig::Type::Wb);
    QCOMPARE(loaded.program.stackSize, 65536);
    QCOMPARE(loaded.filePath(), path);
}

// A project may legitimately select no SDK at all -- a program that drives
// the hardware directly needs no system headers.
void TstProject::noSdkSelectedRoundTrips()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bare.aproj"));

    Project p = Project::createMinimalC(QStringLiteral("bare"));
    p.sdks.selected.clear();
    QVERIFY(p.save(path));

    Project loaded;
    QVERIFY(loaded.load(path));
    QVERIFY(loaded.sdks.selected.isEmpty());
}

// language and output.kind must survive save/load, and an .aproj written
// before those keys existed must still load as a plain C executable and be
// written back without gaining them.
void TstProject::languageAndOutputKindRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/p.aproj");

    Project p = Project::createMinimalAsm(QStringLiteral("mathutil"));
    p.language = ProjectLanguage::Asm;
    p.output.kind = OutputKind::Library;
    QVERIFY(p.save(path));

    Project loaded;
    QVERIFY(loaded.load(path));
    QCOMPARE(loaded.language, ProjectLanguage::Asm);
    QCOMPARE(loaded.output.kind, OutputKind::Library);
}

void TstProject::legacyProjectStaysC()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/old.aproj");

    QFile f(path);
    QVERIFY(f.open(QFile::WriteOnly));
    f.write(R"({"format":"amigaide-project","version":1,"name":"old",
                "output":{"binary":"old","targetDir":"bin","objDir":"build/obj"}})");
    f.close();

    Project p;
    QVERIFY(p.load(path));
    QCOMPARE(p.language, ProjectLanguage::C);
    QCOMPARE(p.output.kind, OutputKind::Executable);

    QVERIFY(p.save(path));
    QVERIFY(f.open(QFile::ReadOnly));
    const QString written = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(!written.contains(QStringLiteral("\"language\"")));
    QVERIFY(!written.contains(QStringLiteral("\"kind\"")));
}

void TstProject::unknownKeysPreserved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("demo.aproj"));

    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(R"({
        "format": "amigaide-project",
        "version": 1,
        "name": "demo",
        "futureFeature": { "someKey": 42 },
        "sources": [ { "path": "src/main.c" } ]
    })");
    file.close();

    Project p;
    QVERIFY(p.load(path));
    QCOMPARE(p.name, QStringLiteral("demo"));

    const QString savedPath = dir.filePath(QStringLiteral("demo2.aproj"));
    QVERIFY(p.save(savedPath));

    QFile saved(savedPath);
    QVERIFY(saved.open(QFile::ReadOnly));
    const QString text = QString::fromUtf8(saved.readAll());
    QVERIFY(text.contains(QStringLiteral("futureFeature")));
    QVERIFY(text.contains(QStringLiteral("someKey")));
}

// A QApplication sets the user's locale; under de_DE strtod() wants ','
// and parson's number parsing failed, so no project with a number in it
// could be opened (vendor/parson.c, "AmigaIDE:").
void TstProject::loadsUnderGermanLocale()
{
    const char *names[] = { "de_DE.UTF-8", "de_DE.utf8", "de_DE", nullptr };
    bool set = false;
    for (int i = 0; names[i] && !set; ++i)
        set = std::setlocale(LC_ALL, names[i]) != nullptr;
    if (!set)
        QSKIP("no German locale installed");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("demo.aproj"));
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(R"({ "format": "amigaide-project", "version": 1, "name": "demo",
        "ratio": 0.25, "sources": [ { "path": "src/main.c" } ] })");
    file.close();

    Project p;
    const bool loaded = p.load(path);
    const QString saved = dir.filePath(QStringLiteral("demo2.aproj"));
    const bool wrote = loaded && p.save(saved);
    QFile out(saved);
    const bool opened = wrote && out.open(QFile::ReadOnly);
    const QString text = opened ? QString::fromUtf8(out.readAll()) : QString();
    std::setlocale(LC_ALL, "C");
    QVERIFY(loaded);
    QCOMPARE(p.name, QStringLiteral("demo"));
    QVERIFY(wrote && opened);
    QVERIFY(text.contains(QStringLiteral("0.25")));
    QVERIFY(text.contains(QStringLiteral("\"version\": 1")) || text.contains(QStringLiteral("\"version\":1")));
}

QTEST_APPLESS_MAIN(TstProject)
// The editor state (open files, active file) lives in <project>.auser,
// not in the .aproj: it changes every session and would keep a versioned
// project "modified" (AmiVCS refuses switch/merge/pull then).
void TstProject::editorStateInUserFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("demo.aproj"));

    Project p = Project::createMinimalC(QStringLiteral("demo"));
    p.editor.openFiles = QStringList{ QStringLiteral("src/main.c"), QStringLiteral("src/util.c") };
    p.editor.activeFile = QStringLiteral("src/util.c");
    QVERIFY(p.save(path));

    QFile aproj(path);
    QVERIFY(aproj.open(QFile::ReadOnly));
    const QByteArray text = aproj.readAll();
    QVERIFY(!text.contains("\"editor\""));
    QVERIFY(!text.contains("openFiles"));
    // as long as "<name>.aproj": fits wherever the project file fits (FFS: 30)
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("demo.auser"))));
    QVERIFY(!QFile::exists(path + QStringLiteral(".user")));

    Project q;
    QVERIFY(q.load(path));
    QCOMPARE(q.editor.openFiles, p.editor.openFiles);
    QCOMPARE(q.editor.activeFile, QStringLiteral("src/util.c"));

    // saving again changes nothing in the .aproj when only the editor moved
    q.editor.activeFile = QStringLiteral("src/main.c");
    QVERIFY(q.save(path));
    QFile again(path);
    QVERIFY(again.open(QFile::ReadOnly));
    QCOMPARE(again.readAll(), text);
}

// An older "<name>.aproj.user" is still read (and left for older IDEs).
void TstProject::legacyUserFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("alt.aproj"));
    Project p = Project::createMinimalC(QStringLiteral("alt"));
    QVERIFY(p.save(path));
    QFile::remove(dir.filePath(QStringLiteral("alt.auser")));
    QFile legacy(path + QStringLiteral(".user"));
    QVERIFY(legacy.open(QFile::WriteOnly));
    legacy.write("{ \"editor\": { \"openFiles\": [\"src/alt.c\"], \"activeFile\": \"src/alt.c\" } }");
    legacy.close();
    Project q;
    QVERIFY(q.load(path));
    QCOMPARE(q.editor.activeFile, QStringLiteral("src/alt.c"));
    QVERIFY(q.save(path));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("alt.auser"))));
    QVERIFY(QFile::exists(path + QStringLiteral(".user")));
}

// The project's long-name switch (.amivcsproject) and the name check.
void TstProject::amigaNames()
{
    QVERIFY(aide_names_fit("abcdefghijklmnopqrstuvwxyz_1.c", 30));
    QVERIFY(!aide_names_fit("abcdefghijklmnopqrstuvwxyz_12.c", 30));
    QVERIFY(aide_names_fit("src/abcdefghijklmnopqrstuvwxyz_1.c", 30));
    QVERIFY(!aide_names_fit("ein_ordner_mit_mehr_als_dreissig_z/a.c", 30));
    QVERIFY(aide_names_fit("\xc3\xa4\xc3\xb6\xc3\xbc", 3));   // characters, not bytes
    QTemporaryDir dir;
    const QByteArray root = dir.path().toUtf8();
    QVERIFY(!aide_project_long_names(root.constData()));
    QFile f(dir.filePath(QStringLiteral(".amivcsproject")));
    QVERIFY(f.open(QFile::WriteOnly));
    f.write("# Projekt\nlongnames=0\nanderes=x\n");
    f.close();
    QVERIFY(aide_project_set_long_names(root.constData(), 1));
    QVERIFY(aide_project_long_names(root.constData()));
    QVERIFY(f.open(QFile::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("# Projekt\nlongnames=1\nanderes=x\n"));
    f.close();
    QVERIFY(aide_project_set_long_names(root.constData(), 0));
    QVERIFY(!aide_project_long_names(root.constData()));
}

#include "tst_project.moc"
