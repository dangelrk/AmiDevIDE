#include "VcsRunner.h"

#include "app/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifndef _WIN32
#include <signal.h>
#endif

QString VcsResult::error() const
{
    if (const char *e = aide_vcs_result_error(&r))
        return QString::fromUtf8(e);
    if (!parsed)
        return QObject::tr("amivcs did not answer as expected%1")
            .arg(output.trimmed().isEmpty() ? QString() : QStringLiteral(":\n") + output.trimmed());
    return QObject::tr("amivcs failed (exit code %1)").arg(exitCode);
}

VcsRunner::VcsRunner(QObject *parent)
    : QObject(parent)
{
    m_progressTimer.setInterval(300);
    connect(&m_progressTimer, &QTimer::timeout, this, &VcsRunner::pollProgress);
}

VcsRunner::~VcsRunner()
{
    for (QProcess *p : { m_read, m_write }) {
        if (p) {
            p->disconnect();
            p->kill();
            p->waitForFinished(2000);
        }
    }
    if (m_readJob.resultPath) aide_vcs_job_free(&m_readJob);
    if (m_writeJob.resultPath) aide_vcs_job_free(&m_writeJob);
}

void VcsRunner::setProjectRoot(const QString &root)
{
    m_root = root;
    m_readQueue.clear();
}

void VcsRunner::checkTool()
{
    const QString prog = AppSettings::instance().amivcsProgram();
    if (prog.isEmpty() || !QFileInfo(prog).isExecutable()) {
        m_available = false;
        m_problem = tr("amivcs was not found. Version control needs the AmiVCS program "
                       "(set its path under Options > Global Settings).");
        emit toolChecked(false, m_problem);
        return;
    }
    run(AIDE_VCS_VERSION, {}, {}, false, QDir::tempPath());
}

bool VcsRunner::run(AideVcsOp op, const QStringList &items, const QString &message, bool flag,
                    const QString &workDir)
{
    Pending p{ op, items, message, flag, workDir };
    const bool writing = aide_vcs_op_writes(op);
    if (writing) {
        if (m_write)
            return false;
        return start(p, true);
    }
    if (m_read || m_write) {          // reads wait for each other and for a write
        m_readQueue.append(p);
        return true;
    }
    return start(p, false);
}

bool VcsRunner::start(const Pending &p, bool writing)
{
    const QString prog = AppSettings::instance().amivcsProgram();
    const QString base = QDir(QDir::tempPath()).filePath(
        QStringLiteral("aide-vcs-%1-%2").arg(QCoreApplication::applicationPid()).arg(++m_jobCounter));

    AideStrList items;
    aide_strlist_init(&items);
    for (const QString &s : p.items)
        aide_strlist_push(&items, s.toUtf8().constData());
    AideVcsJob &job = writing ? m_writeJob : m_readJob;
    const bool built = aide_vcs_job_build(&job, p.op, prog.toUtf8().constData(),
                                          base.toUtf8().constData(), &items,
                                          p.message.toUtf8().constData(), p.flag ? 1 : 0, 200);
    aide_strlist_free(&items);
    if (!built) {
        aide_vcs_job_free(&job);
        auto res = std::make_shared<VcsResult>();
        res->output = tr("Could not write the job files under %1").arg(QDir::tempPath());
        emit finished(p.op, res);
        return false;
    }

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setWorkingDirectory(p.workDir.isEmpty() ? m_root : p.workDir);
    QStringList args;
    for (size_t i = 1; i < job.argv.count; ++i)
        args << QString::fromUtf8(job.argv.items[i]);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, writing](int, QProcess::ExitStatus) { onFinished(proc, writing); });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, writing](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) onFinished(proc, writing);
    });

    if (writing) { m_write = proc; m_writeOp = p.op; }
    else         { m_read = proc;  m_readOp = p.op; }
    if (aide_vcs_op_network(p.op))
        m_progressTimer.start();
    emit started(p.op);
    emit busyChanged();
    proc->start(QString::fromUtf8(job.argv.items[0]), args);
    return true;
}

void VcsRunner::onFinished(QProcess *proc, bool writing)
{
    if (proc != (writing ? m_write : m_read))
        return;   // already handled (errorOccurred + finished)
    AideVcsJob &job = writing ? m_writeJob : m_readJob;
    const AideVcsOp op = writing ? m_writeOp : m_readOp;

    auto res = std::make_shared<VcsResult>();
    res->output = QString::fromUtf8(proc->readAll());
    res->exitCode = proc->exitStatus() == QProcess::NormalExit ? proc->exitCode() : -1;
    res->parsed = job.resultPath && aide_vcs_parse_file(&res->r, job.resultPath);
    aide_vcs_job_free(&job);

    if (writing) m_write = nullptr; else m_read = nullptr;
    proc->deleteLater();
    if (!m_write || !aide_vcs_op_network(m_writeOp))
        m_progressTimer.stop();

    if (op == AIDE_VCS_VERSION) {
        m_available = res->parsed;
        m_problem = m_available ? QString()
                                : tr("This amivcs is too old for the IDE (it needs AmiVCS 0.6 or newer, "
                                     "with --porcelain-out).");
        emit toolChecked(m_available, m_problem);
    }
    emit finished(op, res);
    emit busyChanged();
    startNextRead();
}

void VcsRunner::startNextRead()
{
    if (m_read || m_write || m_readQueue.isEmpty())
        return;
    start(m_readQueue.takeFirst(), false);
}

bool VcsRunner::canCancel() const
{
    return m_write && aide_vcs_op_network(m_writeOp);
}

void VcsRunner::cancel()
{
    if (!canCancel())
        return;
#ifndef _WIN32
    ::kill(static_cast<pid_t>(m_write->processId()), SIGINT);
#else
    m_write->closeWriteChannel();   // no console signal for a GUI child on Windows
#endif
}

// The newest "progress" record of a running network job
void VcsRunner::pollProgress()
{
    if (!m_write || !m_writeJob.resultPath)
        return;
    QFile f(QString::fromUtf8(m_writeJob.resultPath));
    if (!f.open(QFile::ReadOnly))
        return;
    const QByteArray data = f.readAll();
    const int at = data.lastIndexOf("\nprogress\t");
    if (at < 0)
        return;
    const int end = data.indexOf('\n', at + 1);
    const QList<QByteArray> fields = data.mid(at + 1, end < 0 ? -1 : end - at - 1).split('\t');
    if (fields.size() >= 4)
        emit progress(QString::fromUtf8(fields[3]), fields[1].toLongLong(), fields[2].toLongLong());
}
