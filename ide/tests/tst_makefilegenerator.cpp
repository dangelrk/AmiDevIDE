#include "build/MakefileGenerator.h"
#include "build/ToolchainConfig.h"
#include "project/Project.h"
#include "sdk/SdkRegistry.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// Golden-file tests for MakefileGenerator: startup selection, smallData/FPU,
// NDK 3.2 vs 3.9, MUI selection, and exact library link order (plan section 11).
// Volatile absolute paths (project root, toolchain root) are normalized to
// fixed placeholder tokens before comparing against tests/golden/*.makefile.
class TstMakefileGenerator : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void newerInstalledSdkServesTheProject();
    void minimalCWithNdk32();
    void asmProjectWithMinstartAndMui38();
    void asmLibraryIsConcatenated();
    void sharedLibraryHasNoStartupAndStubFirst();
    void minimalBasicWithNdk32();
    void minimalBasicWithNdk32AmigaDialect();
    void basicAmigaDialectTargetsAceAsShipped();
    void minimalCWithNdk32AmigaDialect();
    void asmProjectWithMinstartAndMui38AmigaDialect();

private:
    QString normalize(QString text, const QString &projectRoot) const;
    QString readGolden(const QString &name) const;

    QTemporaryDir m_dir;
};

void TstMakefileGenerator::initTestCase()
{
    QVERIFY(m_dir.isValid());
    SdkRegistry::instance().reload(QStringLiteral("/fake/toolchain"));
}

QString TstMakefileGenerator::normalize(QString text, const QString &projectRoot) const
{
    text.replace(projectRoot, QStringLiteral("<PROJECT_ROOT>"));
    // The host's executable suffix (".exe" on Windows) is the only other
    // host-dependent line; the golden files carry the empty one.
    text.replace(QStringLiteral("E    ?= .exe\n"), QStringLiteral("E    ?= \n"));
    return text;
}

QString TstMakefileGenerator::readGolden(const QString &name) const
{
    QFile file(QStringLiteral(GOLDEN_DIR) + QStringLiteral("/") + name);
    if (!file.open(QFile::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void TstMakefileGenerator::minimalCWithNdk32()
{
    Project project = Project::createMinimalC(QStringLiteral("demo"));
    project.compiler.cpu = QStringLiteral("68000");
    project.compiler.fpu = QStringLiteral("none");
    project.compiler.optLevel = 2;
    project.sdks.selected = { QStringLiteral("ndk-3.2") };
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/demo.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generate(toolchain, project), m_dir.path());
    const QString golden = readGolden(QStringLiteral("minimal_c_ndk32.makefile"));

    QCOMPARE(actual, golden);
}

// ACE BASIC: yap+ace+vasm per source, and ACE's own runtime on the link
// line instead of vbcc's startup.o/-lvc.
// A link library is concatenated, not linked: no vlink call, no STARTUP
// and no LDLIBS, and no __stack.o (defining it would collide with whatever
// program links the library).
void TstMakefileGenerator::asmLibraryIsConcatenated()
{
    Project project = Project::createMinimalAsm(QStringLiteral("mathutil"));
    project.output.binary = QStringLiteral("mathutil.lib");
    project.output.kind = OutputKind::Library;
    project.linker.startup = LinkerConfig::Startup::Std;
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/mathutil.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generate(toolchain, project), m_dir.path());

    QVERIFY(actual.contains(QStringLiteral("cat $(OBJS) > \"$@\"")));
    QVERIFY(!actual.contains(QStringLiteral("$(VLINK)")));
    QVERIFY(!actual.contains(QStringLiteral("STARTUP")));
    QVERIFY(!actual.contains(QStringLiteral("LDLIBS")));
    QVERIFY(!actual.contains(QStringLiteral("__stack")));

    // AmigaDOS spells the same concatenation "join".
    const QString amiga = normalize(
        MakefileGenerator::generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_AMIGADOS),
        m_dir.path());
    QVERIFY(amiga.contains(QStringLiteral("join $(OBJS) AS \"$@\"")));
    QVERIFY(!amiga.contains(QStringLiteral("$(VLINK)")));
}

// A shared .library is linked, but with no startup object -- exec enters
// through the romtag. The startup stub must still be the first object on
// the link line, whatever order the sources are listed in, so its
// "moveq #-1,d0 / rts" lands at offset 0 of the segment.
void TstMakefileGenerator::sharedLibraryHasNoStartupAndStubFirst()
{
    Project project = Project::createMinimalC(QStringLiteral("example"));
    project.output.binary = QStringLiteral("example.library");
    project.output.kind = OutputKind::SharedLibrary;
    project.sources.clear();
    project.sources << SourceFile{ QStringLiteral("src/library.c") }   // listed first
                    << SourceFile{ QStringLiteral("src/libstart.s") }; // must link first
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/example.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generate(toolchain, project), m_dir.path());

    QVERIFY(actual.contains(QStringLiteral("OBJS := $(OBJDIR)/src/libstart.o $(OBJDIR)/src/library.o")));
    QVERIFY(actual.contains(QStringLiteral("$(VLINK) $(LDFLAGS) $(OBJS) $(LDLIBS)")));
    QVERIFY(!actual.contains(QStringLiteral("STARTUP")));
    QVERIFY(!actual.contains(QStringLiteral("__stack")));
    // unlike a static library, it does link, so LDLIBS still applies
    QVERIFY(actual.contains(QStringLiteral("LDLIBS")));
}

void TstMakefileGenerator::minimalBasicWithNdk32()
{
    Project project = Project::createMinimalBasic(QStringLiteral("demobas"));
    project.compiler.cpu = QStringLiteral("68020");
    project.compiler.optLevel = 2;
    project.sdks.selected = { QStringLiteral("ndk-3.2") };
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/demobas.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generate(toolchain, project), m_dir.path());
    const QString golden = readGolden(QStringLiteral("minimal_basic_ndk32.makefile"));

    QCOMPARE(actual, golden);
}

void TstMakefileGenerator::minimalBasicWithNdk32AmigaDialect()
{
    Project project = Project::createMinimalBasic(QStringLiteral("demobas"));
    project.compiler.cpu = QStringLiteral("68000");
    project.compiler.optLevel = 2;
    project.sdks.selected = { QStringLiteral("ndk-3.2") };
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/demobas2.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(
        MakefileGenerator::generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_AMIGADOS), m_dir.path());
    const QString golden = readGolden(QStringLiteral("minimal_basic_ndk32.amiga.makefile"));

    QCOMPARE(actual, golden);
}

// The Amiga runs ACE as shipped: no yap -g, no ace -0, ACE's own assigns.
// 68020 is its default; 68000 comes from an "OPTION 2-" put in front of the
// preprocessed text, and a module still gets its -m when no other option is
// left. The same project on the host keeps the patched ACE's flags.
void TstMakefileGenerator::basicAmigaDialectTargetsAceAsShipped()
{
    Project project = Project::createMinimalBasic(QStringLiteral("modbas"));
    project.sources << SourceFile{ QStringLiteral("src/helper.b") };
    project.compiler.cpu = QStringLiteral("68020");
    project.compiler.optLevel = 0;
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/modbas.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    QString amiga = normalize(
        MakefileGenerator::generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_AMIGADOS), m_dir.path());
    QVERIFY(amiga.contains(QStringLiteral("ACE  = ACE:bin/ace\n")));
    QVERIFY(amiga.contains(QStringLiteral("ACEFLAGS = -2\n")));
    QVERIFY(amiga.contains(QStringLiteral("$(ACE) $(ACEFLAGS)m \"$(OBJDIR)/src/helper.b\"")));
    QVERIFY(amiga.contains(QStringLiteral("STARTUP = $(ACE_LIB)startup.lib\n")));
    QVERIFY(!amiga.contains(QStringLiteral("-g")));
    QVERIFY(!amiga.contains(QStringLiteral("OPTION 2-")));

    project.compiler.cpu = QStringLiteral("68000");
    amiga = normalize(
        MakefileGenerator::generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_AMIGADOS), m_dir.path());
    QVERIFY(amiga.contains(QStringLiteral("ACEFLAGS = \n")));
    QVERIFY(amiga.contains(QStringLiteral("$(ACE) -m \"$(OBJDIR)/src/helper.b\"")));
    QVERIFY(amiga.contains(QStringLiteral("$(ACE) $(ACEFLAGS) \"$(OBJDIR)/src/main.b\"")));
    QCOMPARE(amiga.count(QStringLiteral("\"OPTION 2-\"")), 2);

    const QString host = normalize(MakefileGenerator::generate(toolchain, project), m_dir.path());
    QVERIFY(host.contains(QStringLiteral("ACEFLAGS := -0\n")));
    QVERIFY(host.contains(QStringLiteral("YAPFLAGS := -g ")));
    QVERIFY(!host.contains(QStringLiteral("OPTION 2-")));
}

void TstMakefileGenerator::asmProjectWithMinstartAndMui38()
{
    Project project = Project::createMinimalAsm(QStringLiteral("demoasm"));
    project.compiler.cpu = QStringLiteral("68020");
    project.compiler.fpu = QStringLiteral("68881");
    project.compiler.smallData = true;
    project.linker.startup = LinkerConfig::Startup::MinStart;
    project.linker.strip = true;
    project.sdks.selected = { QStringLiteral("ndk-3.2"), QStringLiteral("mui-3.8") };
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/demoasm.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generate(toolchain, project), m_dir.path());
    const QString golden = readGolden(QStringLiteral("minstart_ndk32_mui38.makefile"));

    QCOMPARE(actual, golden);
}

// Same two projects as above, but golden-tested against the AmigaDOS
// dialect (makedir/delete, " : " rule colons, project-relative
// OBJDIR/BINDIR/TARGET/sources/headers, quoted flag tokens -- see
// aide_makegen.h's dialect doc comment). This is the only way to guard
// that dialect against regressions without real Amiga hardware: there is
// no aide_makegen_host_dialect() switch to flip on this (non-m68k) host,
// so generateForDialect() is called directly instead of generate().
void TstMakefileGenerator::minimalCWithNdk32AmigaDialect()
{
    Project project = Project::createMinimalC(QStringLiteral("demo"));
    project.compiler.cpu = QStringLiteral("68000");
    project.compiler.fpu = QStringLiteral("none");
    project.compiler.optLevel = 2;
    project.sdks.selected = { QStringLiteral("ndk-3.2") };
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/demo.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_AMIGADOS),
                                      m_dir.path());
    const QString golden = readGolden(QStringLiteral("minimal_c_ndk32.amiga.makefile"));

    QCOMPARE(actual, golden);
}

void TstMakefileGenerator::asmProjectWithMinstartAndMui38AmigaDialect()
{
    Project project = Project::createMinimalAsm(QStringLiteral("demoasm"));
    project.compiler.cpu = QStringLiteral("68020");
    project.compiler.fpu = QStringLiteral("68881");
    project.compiler.smallData = true;
    project.linker.startup = LinkerConfig::Startup::MinStart;
    project.linker.strip = true;
    project.sdks.selected = { QStringLiteral("ndk-3.2"), QStringLiteral("mui-3.8") };
    QVERIFY(project.save(m_dir.path() + QStringLiteral("/demoasm.aproj")));

    ToolchainConfig toolchain(QStringLiteral("/fake/toolchain"));
    const QString actual = normalize(MakefileGenerator::generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_AMIGADOS),
                                      m_dir.path());
    const QString golden = readGolden(QStringLiteral("minstart_ndk32_mui38.amiga.makefile"));

    QCOMPARE(actual, golden);
}

// The .aproj names an SDK by name and version only. Installed in a newer
// version, it builds against that one's paths, and the project file does
// not change.
void TstMakefileGenerator::newerInstalledSdkServesTheProject()
{
    QTemporaryDir tc;
    QVERIFY(tc.isValid());
    const QString sdkDir = tc.path() + QStringLiteral("/sdk/FooLib");
    QVERIFY(QDir().mkpath(sdkDir));
    QFile manifest(sdkDir + QStringLiteral("/AmiDevSDK.json"));
    QVERIFY(manifest.open(QFile::WriteOnly));
    manifest.write("{ \"name\": \"Foo Lib\", \"version\": \"1.3\", "
                   "\"includeDirs\": [\"include\"], \"libDirs\": [\"lib\"] }");
    manifest.close();

    Project project = Project::createMinimalC(QStringLiteral("foo"));
    project.sdks.selected = { QStringLiteral("ndk-3.2"), QStringLiteral("Foo-Lib-1.2") };
    const QString aproj = m_dir.path() + QStringLiteral("/foo.aproj");
    QVERIFY(project.save(aproj));

    SdkRegistry::instance().reload(tc.path());
    ToolchainConfig toolchain(tc.path());
    const QString makefile = MakefileGenerator::generate(toolchain, project);
    QVERIFY2(makefile.contains(sdkDir + QStringLiteral("/include")), qPrintable(makefile));
    QVERIFY(makefile.contains(sdkDir + QStringLiteral("/lib")));

    Project reloaded;
    QVERIFY(reloaded.load(aproj));
    QCOMPARE(reloaded.sdks.selected, project.sdks.selected);

    // Not installed at all: the build goes ahead without its paths.
    SdkRegistry::instance().reload(QStringLiteral("/fake/toolchain"));
    QVERIFY(!MakefileGenerator::generate(toolchain, project).contains(sdkDir));
}

QTEST_APPLESS_MAIN(TstMakefileGenerator)
#include "tst_makefilegenerator.moc"
