#include "ProjectAide.h"

#include <cstdlib>
#include <cstring>

namespace {

QStringList toQStringList(const AideStrList &list)
{
    QStringList out;
    out.reserve(int(list.count));
    for (size_t i = 0; i < list.count; ++i)
        out << QString::fromUtf8(list.items[i]);
    return out;
}

void fromQStringList(const QStringList &in, AideStrList *out)
{
    aide_strlist_clear(out);
    for (const QString &s : in) {
        const QByteArray utf8 = s.toUtf8();
        aide_strlist_push(out, utf8.constData());
    }
}

void setStr(char **field, const QString &value)
{
    free(*field);
    const QByteArray utf8 = value.toUtf8();
    *field = static_cast<char *>(malloc(size_t(utf8.size()) + 1));
    memcpy(*field, utf8.constData(), size_t(utf8.size()) + 1);
}

} // namespace

void projectToAide(const Project &p, AideProject *a)
{
    aide_project_init(a);

    setStr(&a->name, p.name);
    a->language = p.language == ProjectLanguage::Basic ? AIDE_LANG_BASIC
                : p.language == ProjectLanguage::Asm   ? AIDE_LANG_ASM
                                                       : AIDE_LANG_C;
    a->output.kind = p.output.kind == OutputKind::Library       ? AIDE_OUTPUT_LIBRARY
                   : p.output.kind == OutputKind::SharedLibrary ? AIDE_OUTPUT_SHARED_LIBRARY
                                                                : AIDE_OUTPUT_EXECUTABLE;
    setStr(&a->output.binary, p.output.binary);
    setStr(&a->output.targetDir, p.output.targetDir);
    setStr(&a->output.objDir, p.output.objDir);

    a->program.type = p.program.type == ProgramConfig::Type::Wb ? AIDE_PROGRAM_WB : AIDE_PROGRAM_CLI;
    a->program.stackSize = p.program.stackSize;
    a->program.iconMode = p.program.iconMode == ProgramConfig::IconMode::File ? AIDE_ICON_FILE : AIDE_ICON_GENERATE;
    setStr(&a->program.iconPath, p.program.iconPath);

    aide_strlist_clear(&a->sources);
    for (const SourceFile &sf : p.sources) {
        const QByteArray utf8 = sf.path.toUtf8();
        aide_strlist_push(&a->sources, utf8.constData());
    }
    fromQStringList(p.headers, &a->headers);
    fromQStringList(p.includePaths, &a->includePaths);
    fromQStringList(p.defines, &a->defines);

    setStr(&a->compiler.cpu, p.compiler.cpu);
    setStr(&a->compiler.fpu, p.compiler.fpu);
    a->compiler.optLevel = p.compiler.optLevel;
    a->compiler.smallData = p.compiler.smallData ? 1 : 0;
    a->compiler.debugSymbols = p.compiler.debugSymbols ? 1 : 0;
    a->compiler.kick1hunks = p.compiler.kick1hunks ? 1 : 0;
    a->compiler.asmNoSym = p.compiler.asmNoSym ? 1 : 0;
    fromQStringList(p.compiler.extraCFlags, &a->compiler.extraCFlags);
    fromQStringList(p.compiler.extraAsmFlags, &a->compiler.extraAsmFlags);

    switch (p.linker.startup) {
    case LinkerConfig::Startup::MinStart: a->linker.startup = AIDE_STARTUP_MINSTART; break;
    case LinkerConfig::Startup::MinRes: a->linker.startup = AIDE_STARTUP_MINRES; break;
    case LinkerConfig::Startup::Custom: a->linker.startup = AIDE_STARTUP_CUSTOM; break;
    case LinkerConfig::Startup::Std: default: a->linker.startup = AIDE_STARTUP_STD; break;
    }
    setStr(&a->linker.customStartupPath, p.linker.customStartupPath);
    fromQStringList(p.linker.libraries, &a->linker.libraries);
    fromQStringList(p.linker.extraLibDirs, &a->linker.extraLibDirs);
    a->linker.strip = p.linker.strip ? 1 : 0;
    fromQStringList(p.linker.extraLdFlags, &a->linker.extraLdFlags);

    fromQStringList(p.sdks.selected, &a->sdks.selected);

    a->run.copyToShared = p.run.copyToShared ? 1 : 0;
    setStr(&a->run.sharedSubdir, p.run.sharedSubdir);
    setStr(&a->run.emulatorConfigOverride, p.run.emulatorConfigOverride);
    fromQStringList(p.run.extraFsUaeArgs, &a->run.extraFsUaeArgs);

    fromQStringList(p.editor.openFiles, &a->editor.openFiles);
    setStr(&a->editor.activeFile, p.editor.activeFile);
}

void aideToProject(const AideProject &a, Project *p)
{
    p->name = QString::fromUtf8(a.name);
    p->language = a.language == AIDE_LANG_BASIC ? ProjectLanguage::Basic
                : a.language == AIDE_LANG_ASM   ? ProjectLanguage::Asm
                                                : ProjectLanguage::C;
    p->output.kind = a.output.kind == AIDE_OUTPUT_LIBRARY        ? OutputKind::Library
                   : a.output.kind == AIDE_OUTPUT_SHARED_LIBRARY ? OutputKind::SharedLibrary
                                                                 : OutputKind::Executable;
    p->output.binary = QString::fromUtf8(a.output.binary);
    p->output.targetDir = QString::fromUtf8(a.output.targetDir);
    p->output.objDir = QString::fromUtf8(a.output.objDir);

    p->program.type = a.program.type == AIDE_PROGRAM_WB ? ProgramConfig::Type::Wb : ProgramConfig::Type::Cli;
    p->program.stackSize = a.program.stackSize;
    p->program.iconMode = a.program.iconMode == AIDE_ICON_FILE ? ProgramConfig::IconMode::File : ProgramConfig::IconMode::Generate;
    p->program.iconPath = QString::fromUtf8(a.program.iconPath);

    p->sources.clear();
    for (size_t i = 0; i < a.sources.count; ++i)
        p->sources << SourceFile{ QString::fromUtf8(a.sources.items[i]) };
    p->headers = toQStringList(a.headers);
    p->includePaths = toQStringList(a.includePaths);
    p->defines = toQStringList(a.defines);

    p->compiler.cpu = QString::fromUtf8(a.compiler.cpu);
    p->compiler.fpu = QString::fromUtf8(a.compiler.fpu);
    p->compiler.optLevel = a.compiler.optLevel;
    p->compiler.smallData = a.compiler.smallData != 0;
    p->compiler.debugSymbols = a.compiler.debugSymbols != 0;
    p->compiler.kick1hunks = a.compiler.kick1hunks != 0;
    p->compiler.asmNoSym = a.compiler.asmNoSym != 0;
    p->compiler.extraCFlags = toQStringList(a.compiler.extraCFlags);
    p->compiler.extraAsmFlags = toQStringList(a.compiler.extraAsmFlags);

    switch (a.linker.startup) {
    case AIDE_STARTUP_MINSTART: p->linker.startup = LinkerConfig::Startup::MinStart; break;
    case AIDE_STARTUP_MINRES: p->linker.startup = LinkerConfig::Startup::MinRes; break;
    case AIDE_STARTUP_CUSTOM: p->linker.startup = LinkerConfig::Startup::Custom; break;
    case AIDE_STARTUP_STD: default: p->linker.startup = LinkerConfig::Startup::Std; break;
    }
    p->linker.customStartupPath = QString::fromUtf8(a.linker.customStartupPath);
    p->linker.libraries = toQStringList(a.linker.libraries);
    p->linker.extraLibDirs = toQStringList(a.linker.extraLibDirs);
    p->linker.strip = a.linker.strip != 0;
    p->linker.extraLdFlags = toQStringList(a.linker.extraLdFlags);

    p->sdks.selected = toQStringList(a.sdks.selected);

    p->run.copyToShared = a.run.copyToShared != 0;
    p->run.sharedSubdir = QString::fromUtf8(a.run.sharedSubdir);
    p->run.emulatorConfigOverride = QString::fromUtf8(a.run.emulatorConfigOverride);
    p->run.extraFsUaeArgs = toQStringList(a.run.extraFsUaeArgs);

    p->editor.openFiles = toQStringList(a.editor.openFiles);
    p->editor.activeFile = QString::fromUtf8(a.editor.activeFile);
}
