#pragma once

#include "panes/Issue.h"
#include "project/Project.h"

#include <QList>
#include <QObject>
#include <QProcess>

// Regenerates build/vc.cfg + build/Makefile, then drives `make` via QProcess
// (merged stdout/stderr, working dir = project root), streaming output lines
// and parsed Issues as they arrive (plan section 7).
class BuildManager : public QObject {
    Q_OBJECT
public:
    explicit BuildManager(QObject *parent = nullptr);

    void build(const Project &project);
    void clean(const Project &project);
    void stop();
    bool isRunning() const;

signals:
    void started();
    void outputLine(const QString &line);
    void issueFound(const Issue &issue);
    // `stopped` is true only when stop() was called for this run -- the
    // process is killed (SIGKILL/TerminateProcess), which QProcess always
    // reports as a "crash", so onProcessFinished() must tell that apart
    // from a genuine crash to report "stopped", not "failed".
    void finished(bool success, bool stopped);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startMake(const Project &project, const QString &target);
    // Writes/patches the Workbench .info icon next to the just-built
    // binary -- a build artifact, generated as part of the build, not
    // deferred to Run/copy-to-shared (that used to write it into the
    // shared-folder copy only, per user feedback: "Icon muss beim BAUEN
    // erzeugt werden, nicht beim starten"). No-op for CLI-type projects.
    void writeIconIfNeeded();

    QProcess *m_process;
    QString m_pendingLine;
    Project m_project;
    bool m_isBuildTarget = false; // true for build() ("all"), false for clean()
    bool m_stopRequested = false; // set by stop(), consumed by onProcessFinished()
};
