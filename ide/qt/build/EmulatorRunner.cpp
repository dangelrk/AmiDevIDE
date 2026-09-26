#include "EmulatorRunner.h"
#include "MakefileGenerator.h"
#include "ToolchainConfig.h"
#include "project/Project.h"
#include "app/AppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

extern "C" {
#include "aide/aide_emulator.h"
}

EmulatorRunner::EmulatorRunner(QObject *parent)
    : QObject(parent)
{
}

// Thin adapter over the portable core/ implementation, like the rest of qt/.
QString EmulatorRunner::defaultEmulatorConfigPath(const QString &toolchainRoot) const
{
    // One generated machine description for every platform now. The old
    // per-OS fs-uae/{mac,lin,win}/ directories are gone: they had drifted
    // apart -- the Windows one mounted the shared folder as DH0 and left the
    // Workbench image out altogether -- and the paths that actually differ
    // are baked in by the bootstrap.
    char buf[1024];
    if (!aide_emulator_default_config_path(toolchainRoot.toUtf8().constData(),
                                           buf, sizeof buf))
        return {};
    return QString::fromUtf8(buf);
}

QString EmulatorRunner::findEmulatorBinary()
{
    char buf[1024];
    if (!aide_emulator_find_binary(buf, sizeof buf))
        return {};
    return QString::fromUtf8(buf);
}

bool EmulatorRunner::isEmulatorRunning() const
{
    const QString exeName = QFileInfo(AppSettings::instance().fsUaeBinary()).fileName();
    return aide_emulator_is_running(exeName.toUtf8().constData()) != 0;
}

void EmulatorRunner::run(const Project &project)
{
    const QString binaryPath = MakefileGenerator::targetBinaryPath(project);
    if (!QFile::exists(binaryPath)) {
        emit errorOccurred(tr("Binary not found -- build the project first."));
        return;
    }

    AppSettings &settings = AppSettings::instance();

    QString destDir = settings.sharedDir();
    if (!project.run.sharedSubdir.isEmpty())
        destDir += QLatin1Char('/') + project.run.sharedSubdir;

    if (!QDir().mkpath(destDir)) {
        emit errorOccurred(tr("Could not create shared directory: %1").arg(destDir));
        return;
    }

    const QString destBinary = destDir + QLatin1Char('/') + project.output.binary;
    QFile::remove(destBinary);
    if (!QFile::copy(binaryPath, destBinary)) {
        emit errorOccurred(tr("Could not copy binary to %1").arg(destBinary));
        return;
    }

    // The .info icon is a build artifact -- BuildManager writes/patches it
    // next to binaryPath as part of the build itself (per user feedback:
    // "Icon muss beim BAUEN erzeugt werden, nicht beim starten"), so Run
    // only needs to copy whatever's already there over to the shared
    // folder, same as the binary. Nothing to do for CLI-type projects
    // (BuildManager never wrote one) or if the project hasn't been
    // rebuilt since Workbench/icon settings changed -- not this
    // function's job to paper over that.
    if (project.program.type == ProgramConfig::Type::Wb) {
        const QString sourceInfo = binaryPath + QStringLiteral(".info");
        const QString destInfo = destBinary + QStringLiteral(".info");
        if (QFile::exists(sourceInfo)) {
            QFile::remove(destInfo);
            if (!QFile::copy(sourceInfo, destInfo))
                emit errorOccurred(tr("Could not copy Workbench icon to %1").arg(destInfo));
        }
    }

    if (isEmulatorRunning()) {
        emit statusMessage(tr("Copied to SHARED: -- FS-UAE is already running; "
                              "restart the program from the Amiga shell."));
        return;
    }

    ToolchainConfig toolchain(settings.toolchainRoot());
    const QString configPath = project.run.emulatorConfigOverride.isEmpty()
        ? defaultEmulatorConfigPath(toolchain.root())
        : project.run.emulatorConfigOverride;

    QStringList args = { configPath };
    args += project.run.extraFsUaeArgs;

    if (!QProcess::startDetached(settings.fsUaeBinary(), args)) {
        emit errorOccurred(tr("Could not launch FS-UAE (%1)").arg(settings.fsUaeBinary()));
        return;
    }

    emit statusMessage(tr("Launching FS-UAE..."));
}
