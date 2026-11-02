#pragma once

// Runs amivcs (AmiVCS, an optional external tool) for the IDE: one
// QProcess per job in the project directory, the result read from the
// job's --porcelain-out file (aide_vcs.c). Two lanes, as in
// docs/AMIVCS-PLAN.md section 4: reading jobs (status, log, branches,
// version) queue behind each other; a writing job runs alone -- the
// caller refuses it while a build runs, and reading jobs wait for it.

#include <QHash>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QMetaType>
#include <QTimer>

#include <memory>

extern "C" {
#include "aide/aide_vcs.h"
}

class QProcess;

// A parsed result; frees itself.
struct VcsResult {
    AideVcsResult r;
    bool parsed = false;      // a usable porcelain file was there
    int exitCode = -1;
    QString output;           // stdout/stderr of the process, for the log
    VcsResult() { aide_vcs_result_init(&r); }
    ~VcsResult() { aide_vcs_result_free(&r); }
    VcsResult(const VcsResult &) = delete;
    VcsResult &operator=(const VcsResult &) = delete;
    bool ok() const { return parsed && r.ended && r.rc == 0; }
    QString error() const;    // newest error message, or a fallback text
};
using VcsResultPtr = std::shared_ptr<VcsResult>;
Q_DECLARE_METATYPE(VcsResultPtr)

class VcsRunner : public QObject {
    Q_OBJECT
public:
    explicit VcsRunner(QObject *parent = nullptr);
    ~VcsRunner() override;

    // Working directory of the jobs (the project root). Changing it drops
    // queued reading jobs.
    void setProjectRoot(const QString &root);
    QString projectRoot() const { return m_root; }

    // The amivcs program from AppSettings; checkTool() asks it for its
    // format (amivcs version) and emits toolChecked().
    void checkTool();
    bool toolAvailable() const { return m_available; }
    QString toolProblem() const { return m_problem; }

    // Starts a job (items/message/flag as aide_vcs_job_build()). A writing
    // job is refused (returns false) while another writing job runs.
    // workDir overrides the project root (clone).
    bool run(AideVcsOp op, const QStringList &items = {}, const QString &message = {},
             bool flag = false, const QString &workDir = {});

    bool writing() const { return m_write != nullptr; }
    bool busy() const { return m_write || m_read; }
    AideVcsOp writingOp() const { return m_writeOp; }

    // Network jobs only (push, pull, clone): SIGINT, which amivcs honours
    // while downloading and ignores once it writes the working tree.
    bool canCancel() const;
    void cancel();

signals:
    void toolChecked(bool available, const QString &problem);
    void started(int op);
    void finished(int op, VcsResultPtr result);
    void progress(const QString &label, qint64 done, qint64 total);
    void busyChanged();

private:
    struct Pending {
        AideVcsOp op;
        QStringList items;
        QString message;
        bool flag;
        QString workDir;
    };
    bool start(const Pending &p, bool writing);
    void onFinished(QProcess *proc, bool writing);
    void startNextRead();
    void pollProgress();

    QString m_root;
    bool m_available = false;
    QString m_problem;
    QProcess *m_read = nullptr;
    QProcess *m_write = nullptr;
    AideVcsOp m_readOp = AIDE_VCS_STATUS;
    AideVcsOp m_writeOp = AIDE_VCS_STATUS;
    AideVcsJob m_readJob{};
    AideVcsJob m_writeJob{};
    QList<Pending> m_readQueue;
    QTimer m_progressTimer;
    int m_jobCounter = 0;
};
