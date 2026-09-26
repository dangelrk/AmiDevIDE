#include "VcConfigGenerator.h"
#include "project/Project.h"

#include "aide/aide_vccfg.h"

#include <QDir>
#include <QFile>

#include <cstdlib>

// Thin adapter over the portable core/ implementation (core/src/aide_vccfg.c,
// shared with the Amiga/MUI build).

namespace VcConfigGenerator {

namespace {
// aide_vccfg_generate only reads toolchain->root and project->compiler.smallData
// (see core/src/aide_vccfg.c) -- no need to copy the rest of Project's fields.
void toAideProject(const Project &p, AideProject *a)
{
    aide_project_init(a);
    a->compiler.smallData = p.compiler.smallData ? 1 : 0;
}
}

QString generate(const ToolchainConfig &toolchain, const Project &project)
{
    AideToolchainConfig tc;
    const QByteArray rootUtf8 = toolchain.root().toUtf8();
    aide_toolchain_init(&tc, rootUtf8.constData());

    AideProject a;
    toAideProject(project, &a);

    char *result = aide_vccfg_generate(&tc, &a);
    const QString qResult = QString::fromUtf8(result);

    free(result);
    aide_project_free(&a);
    aide_toolchain_free(&tc);
    return qResult;
}

bool writeToProject(const ToolchainConfig &toolchain, const Project &project)
{
    const QString buildDir = project.projectRoot() + QStringLiteral("/build");
    if (!QDir().mkpath(buildDir))
        return false;

    QFile file(buildDir + QStringLiteral("/vc.cfg"));
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
        return false;

    file.write(generate(toolchain, project).toLatin1());
    return true;
}

} // namespace VcConfigGenerator
