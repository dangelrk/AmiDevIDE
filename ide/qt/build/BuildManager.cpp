#include "BuildManager.h"
#include "ToolchainConfig.h"
#include "VcConfigGenerator.h"
#include "MakefileGenerator.h"
#include "DiagnosticsParser.h"
#include "IconWriter.h"
#include "project/Project.h"
#include "app/AppSettings.h"

BuildManager::BuildManager(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &BuildManager::onReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BuildManager::onProcessFinished);
}

bool BuildManager::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void BuildManager::stop()
{
    if (isRunning()) {
        m_stopRequested = true;
        m_process->kill();
    }
}

void BuildManager::startMake(const Project &project, const QString &target)
{
    if (isRunning())
        return;

    ToolchainConfig toolchain(AppSettings::instance().toolchainRoot());

    // vc.cfg is read by vc, which only a C project ever invokes.
    if (project.language == ProjectLanguage::C
        && !VcConfigGenerator::writeToProject(toolchain, project)) {
        emit outputLine(tr("Could not write build/vc.cfg"));
        emit finished(false, false);
        return;
    }
    if (!MakefileGenerator::writeToProject(toolchain, project)) {
        emit outputLine(tr("Could not write build/Makefile"));
        emit finished(false, false);
        return;
    }

    m_pendingLine.clear();
    m_process->setWorkingDirectory(project.projectRoot());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pathSep =
#ifdef Q_OS_WIN
        QStringLiteral(";");
#else
        QStringLiteral(":");
#endif
    env.insert(QStringLiteral("PATH"), toolchain.root() + QStringLiteral("/bin") + pathSep + env.value(QStringLiteral("PATH")));
    m_process->setProcessEnvironment(env);

    const QStringList args = {
        QStringLiteral("-f"), QStringLiteral("build/Makefile"),
        QStringLiteral("VBCC=%1").arg(toolchain.root()),
        QStringLiteral("E=%1").arg(toolchain.exeSuffix()),
        target,
    };

    emit started();
    m_process->start(AppSettings::instance().makeProgram(), args);
}

void BuildManager::build(const Project &project)
{
    m_project = project;
    m_isBuildTarget = true;
    startMake(project, QStringLiteral("all"));
}

void BuildManager::clean(const Project &project)
{
    m_isBuildTarget = false;
    startMake(project, QStringLiteral("clean"));
}

void BuildManager::writeIconIfNeeded()
{
    if (m_project.program.type != ProgramConfig::Type::Wb)
        return;

    const QString binaryPath = MakefileGenerator::targetBinaryPath(m_project);
    const QString infoPath = binaryPath + QStringLiteral(".info");

    bool ok;
    if (m_project.program.iconMode == ProgramConfig::IconMode::File && !m_project.program.iconPath.isEmpty()) {
        ok = IconWriter::copyIcon(m_project.absolutePath(m_project.program.iconPath), infoPath,
                                   /*patchStackSize=*/true, m_project.program.stackSize);
    } else {
        ok = IconWriter::writeDefaultIcon(infoPath, m_project.program.stackSize);
    }
    if (!ok)
        emit outputLine(tr("Could not write Workbench icon %1").arg(infoPath));
}

void BuildManager::onReadyRead()
{
    m_pendingLine += QString::fromLocal8Bit(m_process->readAllStandardOutput());

    int newlineIndex;
    while ((newlineIndex = m_pendingLine.indexOf('\n')) >= 0) {
        QString line = m_pendingLine.left(newlineIndex);
        if (line.endsWith('\r'))
            line.chop(1);
        m_pendingLine.remove(0, newlineIndex + 1);

        emit outputLine(line);
        if (auto issue = DiagnosticsParser::parseLine(line))
            emit issueFound(*issue);
    }
}

void BuildManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_pendingLine.isEmpty()) {
        emit outputLine(m_pendingLine);
        if (auto issue = DiagnosticsParser::parseLine(m_pendingLine))
            emit issueFound(*issue);
        m_pendingLine.clear();
    }

    const bool stopped = m_stopRequested;
    m_stopRequested = false;

    const bool success = !stopped && status == QProcess::NormalExit && exitCode == 0;
    if (success && m_isBuildTarget)
        writeIconIfNeeded();
    emit finished(success, stopped);
}
