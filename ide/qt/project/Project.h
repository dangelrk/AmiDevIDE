#pragma once

#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

struct SourceFile {
    QString path; // relative to project root
};

// Which compiler drives the project. Not a per-file property: an ACE BASIC
// program brings its own startup code and runtime (ACE's startup.lib/
// db.lib) where a C program brings vbcc's (startup.o/vc.lib), and the two
// cannot be linked into one binary.
enum class ProjectLanguage { C, Basic, Asm };

// What the link step produces. A library is concatenated rather than
// linked -- see AideOutputKind in core for why, and note this is a static
// link library, not an AmigaOS shared .library.
enum class OutputKind { Executable, Library, SharedLibrary };

struct ProgramConfig {
    enum class Type { Cli, Wb };
    enum class IconMode { Generate, File };

    Type type = Type::Cli;
    int stackSize = 8192;
    IconMode iconMode = IconMode::Generate;
    QString iconPath; // used when iconMode == File
};

struct CompilerConfig {
    QString cpu = QStringLiteral("68000"); // 68000..68060
    QString fpu = QStringLiteral("none");  // none|68881|68882|68040|68060|softfloat
    int optLevel = 2;
    bool smallData = false;
    bool debugSymbols = false;
    bool kick1hunks = true;
    bool asmNoSym = true;
    QStringList extraCFlags;
    QStringList extraAsmFlags;
};

struct LinkerConfig {
    enum class Startup { Std, MinStart, MinRes, Custom };

    Startup startup = Startup::Std;
    QString customStartupPath;
    QStringList libraries; // ordered: exact link order
    QStringList extraLibDirs;
    bool strip = false;
    QStringList extraLdFlags;
};

struct SdkSelection {
    // Every SDK the project builds against, by registry id, in include-search
    // order. One list rather than a mandatory NDK plus an optional MUI plus
    // the rest: there is one NDK and one MUI now, and neither is compulsory.
    // Mirrors AideSdkSelection -- see aide_project.h for the migration.
    QStringList selected = { QStringLiteral("ndk-3.2") };
};

struct RunConfig {
    bool copyToShared = true;
    QString sharedSubdir;
    QString emulatorConfigOverride;
    QStringList extraFsUaeArgs;
};

struct EditorState {
    QStringList openFiles;
    QString activeFile;
};

struct OutputConfig {
    OutputKind kind = OutputKind::Executable;
    QString binary;
    QString targetDir = QStringLiteral("bin");
    QString objDir = QStringLiteral("build/obj");
};

// In-memory model of a .aproj file (JSON, paths relative to the project
// root = the directory containing the .aproj file). Unknown top-level JSON
// keys are preserved verbatim across load/save for forward compatibility.
class Project {
public:
    bool load(const QString &path);
    bool save(const QString &path = QString());

    QString filePath() const { return m_filePath; }
    // Lets a template resolve absolutePath() before the project has ever
    // been saved (New Project writes its starter files first).
    void setFilePathForTemplates(const QString &path) { m_filePath = path; }
    QString projectRoot() const { return QFileInfo(m_filePath).absolutePath(); }
    QString absolutePath(const QString &relative) const;

    // De-duplicated absolute paths of every source + header file in the
    // project (used by project-wide search / find-references).
    QStringList allProjectFiles() const;

    static Project createMinimalC(const QString &name);
    static Project createMinimalAsm(const QString &name);
    static Project createMinimalBasic(const QString &name);

    QString name;
    ProjectLanguage language = ProjectLanguage::C;
    OutputConfig output;
    ProgramConfig program;
    QList<SourceFile> sources;
    QStringList headers;
    QStringList includePaths;
    QStringList defines;
    CompilerConfig compiler;
    LinkerConfig linker;
    SdkSelection sdks;
    RunConfig run;
    EditorState editor;

private:
    QString m_filePath;
    QString m_rawJsonText; // full text of the last loaded/saved document, used
                            // to preserve unknown top-level keys (see
                            // core/src/aide_project.c's raw_json_passthrough)
};
