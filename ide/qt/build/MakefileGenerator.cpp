#include "MakefileGenerator.h"
#include "project/Project.h"
#include "project/ProjectAide.h"
#include "sdk/SdkDescriptorAide.h"
#include "sdk/SdkRegistry.h"

#include "aide/aide_makegen.h"

#include <QDir>
#include <QFile>

#include <cstdlib>

// Thin adapter over the portable core/ implementation (core/src/aide_makegen.c,
// golden-file tested and shared with the Amiga/MUI build). This file only
// converts Project +
// ToolchainConfig + SdkRegistry into their AideProject/AideToolchainConfig/
// AideSdkList counterparts and back.

namespace MakefileGenerator {

namespace {

void toAideToolchain(const ToolchainConfig &tc, AideToolchainConfig *a)
{
    const QByteArray rootUtf8 = tc.root().toUtf8();
    aide_toolchain_init(a, rootUtf8.constData());
}

void toAideSdkRegistry(AideSdkList *out)
{
    aide_sdk_list_init(out);
    for (const SdkDescriptor &d : SdkRegistry::instance().allSdks()) {
        AideSdkDescriptor a;
        sdkDescriptorToAide(d, &a);
        aide_sdk_list_push(out, &a);
        aide_sdk_descriptor_free(&a);
    }
}

} // namespace

bool needsStackObject(const Project &project)
{
    AideProject a;
    projectToAide(project, &a);
    const bool result = aide_makegen_needs_stack_object(&a) != 0;
    aide_project_free(&a);
    return result;
}

QString stackObjectSource(int stackSize)
{
    char *text = aide_makegen_stack_object_source(stackSize);
    const QString result = QString::fromUtf8(text);
    free(text);
    return result;
}

QString targetBinaryPath(const Project &project)
{
    AideProject a;
    projectToAide(project, &a);
    const QByteArray rootUtf8 = project.projectRoot().toUtf8();
    char *text = aide_makegen_target_binary_path(&a, rootUtf8.constData());
    const QString result = QString::fromUtf8(text);
    free(text);
    aide_project_free(&a);
    return result;
}

QString generate(const ToolchainConfig &toolchain, const Project &project)
{
    return generateForDialect(toolchain, project, AIDE_MAKE_DIALECT_POSIX);
}

QString generateForDialect(const ToolchainConfig &toolchain, const Project &project, AideMakeDialect dialect)
{
    AideToolchainConfig tc;
    toAideToolchain(toolchain, &tc);

    AideProject a;
    projectToAide(project, &a);

    AideSdkList registry;
    toAideSdkRegistry(&registry);

    const QByteArray rootUtf8 = project.projectRoot().toUtf8();
    char *text = aide_makegen_generate_dialect(&tc, &a, rootUtf8.constData(), &registry, dialect);
    const QString result = QString::fromUtf8(text);

    free(text);
    aide_sdk_list_free(&registry);
    aide_project_free(&a);
    aide_toolchain_free(&tc);
    return result;
}

bool writeToProject(const ToolchainConfig &toolchain, const Project &project)
{
    const QString buildDir = project.projectRoot() + QStringLiteral("/build");
    if (!QDir().mkpath(buildDir))
        return false;

    if (needsStackObject(project)) {
        QFile stackFile(buildDir + QStringLiteral("/__stack.c"));
        if (!stackFile.open(QFile::WriteOnly | QFile::Truncate))
            return false;
        stackFile.write(stackObjectSource(project.program.stackSize).toLatin1());
    }

    QFile file(buildDir + QStringLiteral("/Makefile"));
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
        return false;

    file.write(generate(toolchain, project).toLatin1());
    return true;
}

} // namespace MakefileGenerator
