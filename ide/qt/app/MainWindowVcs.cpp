// Version control in the Qt IDE: AmiVCS as an external tool (amivcs),
// see docs/AMIVCS-PLAN.md. Everything here is MainWindow glue; job
// building and result parsing live in core (aide_vcs.c), running the
// program in VcsRunner.

#include "MainWindow.h"

#include "AppSettings.h"
#include "editor/EditorTabWidget.h"
#include "editor/EditorWidget.h"
#include "panes/OutputPane.h"
#include "project/ProjectTreeDock.h"
#include "vcs/HistoryDock.h"
#include "vcs/VcsDialogs.h"
#include "vcs/VcsDock.h"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <cstdlib>

static QString opName(int op)
{
    switch (op) {
    case AIDE_VCS_INIT:       return QObject::tr("Create repository");
    case AIDE_VCS_ADD:        return QObject::tr("Stage");
    case AIDE_VCS_UNSTAGE:    return QObject::tr("Unstage");
    case AIDE_VCS_COMMIT:     return QObject::tr("Commit");
    case AIDE_VCS_BRANCH_NEW: return QObject::tr("New branch");
    case AIDE_VCS_SWITCH:     return QObject::tr("Switch branch");
    case AIDE_VCS_MERGE:      return QObject::tr("Merge");
    case AIDE_VCS_CHECKOUT:   return QObject::tr("Restore files");
    case AIDE_VCS_PUSH:       return QObject::tr("Push");
    case AIDE_VCS_PULL:       return QObject::tr("Pull");
    case AIDE_VCS_CLONE:      return QObject::tr("Clone");
    case AIDE_VCS_MERGE_ABORT: return QObject::tr("Abort merge");
    case AIDE_VCS_RESOLVE:    return QObject::tr("Resolve conflict");
    case AIDE_VCS_DIFF:       return QObject::tr("Diff");
    default:                  return QObject::tr("Version control");
    }
}

void MainWindow::createVcs()
{
    m_vcs = new VcsRunner(this);
    m_vcsDock = new VcsDock(this);
    m_historyDock = new HistoryDock(this);

    m_vcsDockWidget = new QDockWidget(tr("Version Control"), this);
    m_vcsDockWidget->setObjectName(QStringLiteral("VcsDock"));
    m_vcsDockWidget->setWidget(m_vcsDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_vcsDockWidget);
    tabifyDockWidget(m_projectTreeDockWidget, m_vcsDockWidget);

    m_historyDockWidget = new QDockWidget(tr("History"), this);
    m_historyDockWidget->setObjectName(QStringLiteral("HistoryDock"));
    m_historyDockWidget->setWidget(m_historyDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_historyDockWidget);
    tabifyDockWidget(m_bottomDockWidget, m_historyDockWidget);
    m_projectTreeDockWidget->raise();
    m_bottomDockWidget->raise();

    QMenu *menu = new QMenu(tr("&VCS"), this);
    menuBar()->insertMenu(menuBar()->actions().value(menuBar()->actions().size() - 2), menu);
    m_vcsInitAct = menu->addAction(tr("&Create Repository"), this, &MainWindow::vcsInit);
    m_vcsRepoActions << menu->addAction(tr("&Refresh"), this, [this] { vcsRefresh(true); });
    m_vcsRepoActions << menu->addAction(tr("Stage &All"), this, [this] { vcsStartWrite(AIDE_VCS_ADD, { QStringLiteral("-A") }); });
    m_vcsRepoActions << menu->addAction(tr("&Commit..."), this, [this] {
        m_vcsDockWidget->show();
        m_vcsDockWidget->raise();
    });
    menu->addSeparator();
    m_vcsRepoActions << menu->addAction(tr("&New Branch..."), this, &MainWindow::vcsNewBranch);
    m_vcsRepoActions << menu->addAction(tr("&Switch Branch..."), this, [this] { vcsChooseBranch(AIDE_VCS_SWITCH); });
    m_vcsRepoActions << menu->addAction(tr("&Merge Branch..."), this, [this] { vcsChooseBranch(AIDE_VCS_MERGE); });
    m_vcsRepoActions << menu->addAction(tr("&Abort Merge"), this, &MainWindow::vcsAbortMerge);
    m_vcsRepoActions << menu->addAction(tr("Restore &Deleted Files"), this, [this] {
        if (vcsPrepareTouch(tr("Restore the committed versions of deleted files")))
            vcsStartWrite(AIDE_VCS_CHECKOUT);
    });
    menu->addSeparator();
    m_vcsRepoActions << menu->addAction(tr("P&ull"), this, &MainWindow::vcsPull);
    m_vcsRepoActions << menu->addAction(tr("&Push"), this, &MainWindow::vcsPush);
    m_vcsCloneAct = menu->addAction(tr("C&lone..."), this, &MainWindow::vcsClone);
    m_vcsRepoActions << menu->addAction(tr("S&ettings..."), this, [this] { vcsSettings(); });
    m_vcsCancelAct = menu->addAction(tr("Cancel &Transfer"), m_vcs, &VcsRunner::cancel);
    m_vcsCancelAct->setEnabled(false);

    QMenu *viewMenu = nullptr;
    for (QAction *a : menuBar()->actions())
        if (a->menu() && a->text() == tr("&View")) viewMenu = a->menu();
    if (viewMenu) {
        viewMenu->addAction(m_vcsDockWidget->toggleViewAction());
        viewMenu->addAction(m_historyDockWidget->toggleViewAction());
    }

    connect(m_vcs, &VcsRunner::toolChecked, this, &MainWindow::vcsOnToolChecked);
    connect(m_vcs, &VcsRunner::started, this, &MainWindow::vcsOnStarted);
    connect(m_vcs, &VcsRunner::finished, this, &MainWindow::vcsOnFinished);
    connect(m_vcs, &VcsRunner::progress, this, [this](const QString &label, qint64 done, qint64 total) {
        statusBar()->showMessage(total > 0 ? tr("%1: %2 of %3").arg(label).arg(done).arg(total)
                                           : tr("%1: %2").arg(label).arg(done));
    });

    connect(m_vcsDock, &VcsDock::refreshRequested, this, [this] { vcsRefresh(true); });
    connect(m_vcsDock, &VcsDock::initRequested, this, &MainWindow::vcsInit);
    connect(m_vcsDock, &VcsDock::stageRequested, this, [this](const QStringList &p) { vcsStartWrite(AIDE_VCS_ADD, p); });
    connect(m_vcsDock, &VcsDock::unstageRequested, this, [this](const QStringList &p) { vcsStartWrite(AIDE_VCS_UNSTAGE, p); });
    connect(m_vcsDock, &VcsDock::stageAllRequested, this, [this] { vcsStartWrite(AIDE_VCS_ADD, { QStringLiteral("-A") }); });
    connect(m_vcsDock, &VcsDock::commitRequested, this, &MainWindow::vcsCommit);
    connect(m_vcsDock, &VcsDock::diffRequested, this, [this](const QString &rel, bool staged) {
        m_vcsDiffTitle = rel + (staged ? tr(" (staged)") : QString());
        m_vcs->run(AIDE_VCS_DIFF, { rel }, {}, staged);
    });
    connect(m_vcsDock, &VcsDock::abortMergeRequested, this, &MainWindow::vcsAbortMerge);
    connect(m_vcsDock, &VcsDock::resolveRequested, this, [this](const QStringList &p, bool theirs) {
        if (!m_editorTabs->saveAllModified()) return;
        saveProject();   // the open tabs come back after the reload
        vcsStartWrite(AIDE_VCS_RESOLVE, p, {}, theirs);
    });
    connect(m_vcsDock, &VcsDock::markResolvedRequested, this, [this](const QStringList &p) {
        if (!m_editorTabs->saveAllModified()) {   // stage what the editor shows
            QMessageBox::warning(this, opName(AIDE_VCS_ADD), tr("Not every file could be saved; nothing was marked."));
            return;
        }
        vcsStartWrite(AIDE_VCS_ADD, p);
    });
    connect(m_vcsDock, &VcsDock::fileActivated, this, [this](const QString &rel) {
        if (m_project && QFile::exists(m_project->absolutePath(rel)))
            openFileAt(m_project->absolutePath(rel));
    });

    // a saved file changes the status: refresh, bundled
    m_vcsRefreshTimer.setSingleShot(true);
    m_vcsRefreshTimer.setInterval(2000);
    connect(&m_vcsRefreshTimer, &QTimer::timeout, this, [this] { vcsRefresh(false); });
    connect(m_editorTabs, &EditorTabWidget::fileContentsPersisted, this, [this] { vcsRefreshSoon(); });

    m_vcs->checkTool();
    vcsProjectChanged();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        vcsRefreshSoon();   // files may have changed outside the IDE
}

void MainWindow::vcsOnToolChecked(bool available, const QString &problem)
{
    if (!available)
        m_outputPane->appendLine(tr("Version control: %1").arg(problem));
    vcsProjectChanged();
}

bool MainWindow::vcsTreeDirty() const
{
    if (!m_vcsStatus) return false;
    const AideVcsResult &r = m_vcsStatus->r;
    for (size_t i = 0; i < r.changeCount; ++i)
        if (r.changes[i].staged == 'S' || r.changes[i].kind != 'N')
            return true;
    return false;
}

void MainWindow::vcsProjectChanged()
{
    const bool tool = m_vcs && m_vcs->toolAvailable();
    const bool repo = tool && m_project && aide_vcs_present(m_project->projectRoot().toUtf8().constData());
    m_vcsStatus.reset();
    if (m_projectTree) m_projectTree->setVcsStates({});
    if (m_historyDock) m_historyDock->clear();

    if (m_vcs) m_vcs->setProjectRoot(m_project ? m_project->projectRoot() : QString());
    for (QAction *a : m_vcsRepoActions) a->setEnabled(repo && !m_vcsLocked);
    if (m_vcsInitAct) m_vcsInitAct->setEnabled(tool && m_project && !repo && !m_vcsLocked);
    if (m_vcsCloneAct) m_vcsCloneAct->setEnabled(tool && !m_vcsLocked);

    if (!m_vcsDock) return;
    if (!tool) m_vcsDock->showUnavailable(m_vcs ? m_vcs->toolProblem() : QString());
    else if (!m_project) m_vcsDock->showNoProject();
    else if (!repo) m_vcsDock->showNoRepository();
    else {
        vcsCheckIgnore();
        vcsRefresh(true);
    }
}

void MainWindow::vcsRefresh(bool withLog)
{
    if (!m_vcs || !m_vcs->toolAvailable() || !m_project
        || !aide_vcs_present(m_project->projectRoot().toUtf8().constData()))
        return;
    m_vcs->run(AIDE_VCS_STATUS);
    if (withLog)
        m_vcs->run(AIDE_VCS_LOG);
}

void MainWindow::vcsRefreshSoon()
{
    if (m_vcs && m_vcs->toolAvailable() && !m_vcs->writing())
        m_vcsRefreshTimer.start();
}

// Switch, merge, pull, restore rewrite files: everything open must be on
// disk first (and is reloaded afterwards, vcsReloadAfterTouch()).
bool MainWindow::vcsPrepareTouch(const QString &what)
{
    if (m_building) {
        QMessageBox::information(this, what, tr("A build is running. Try again when it has finished."));
        return false;
    }
    if (QMessageBox::question(this, what,
                              tr("%1 can change files of the project.\n\n"
                                 "All open files and the project are saved first and reloaded afterwards. "
                                 "Continue?").arg(what)) != QMessageBox::Yes)
        return false;
    if (!m_editorTabs->saveAllModified()) {
        QMessageBox::warning(this, what, tr("Not every file could be saved; nothing was changed."));
        return false;
    }
    saveProject();
    return true;
}

bool MainWindow::vcsStartWrite(AideVcsOp op, const QStringList &items, const QString &message, bool flag,
                               const QString &workDir)
{
    if (m_building) {
        statusBar()->showMessage(tr("A build is running -- version control has to wait."), 4000);
        return false;
    }
    if (!m_vcs->run(op, items, message, flag, workDir)) {
        statusBar()->showMessage(tr("Another version-control job is still running."), 4000);
        return false;
    }
    return true;
}

void MainWindow::vcsSetLocked(bool locked)
{
    m_vcsLocked = locked;
    const bool touching = locked && aide_vcs_op_touches_files(m_vcs->writingOp());
    // no saving while amivcs rewrites the working tree
    for (int i = 0; i < m_editorTabs->count(); ++i)
        if (auto *e = qobject_cast<EditorWidget *>(m_editorTabs->widget(i)))
            e->setReadOnly(touching);
    setBuildActionsEnabled(!locked && !m_building);
    if (m_stopBuildAct && locked) m_stopBuildAct->setEnabled(false);
    m_vcsDock->setBusy(locked);
    m_vcsCancelAct->setEnabled(m_vcs->canCancel());
    for (QAction *a : m_vcsRepoActions) a->setEnabled(!locked);
    m_vcsInitAct->setEnabled(!locked && m_project && !aide_vcs_present(m_project->projectRoot().toUtf8().constData()));
    m_vcsCloneAct->setEnabled(!locked);
}

void MainWindow::vcsOnStarted(int op)
{
    if (!aide_vcs_op_writes(static_cast<AideVcsOp>(op)))
        return;
    vcsSetLocked(true);
    statusBar()->showMessage(tr("%1 ...").arg(opName(op)));
    if (aide_vcs_op_network(static_cast<AideVcsOp>(op))) {
        m_bottomDockWidget->raise();
        m_outputPane->appendLine(tr("--- %1 ---").arg(opName(op)));
    }
}

// Every message of a job goes to the output pane; an error of a job the
// user started also comes up as a requester.
void MainWindow::vcsReport(const VcsResultPtr &res, const QString &what, bool requester)
{
    for (size_t i = 0; i < res->r.messageCount; ++i) {
        const AideVcsMessage &m = res->r.messages[i];
        m_outputPane->appendLine(QStringLiteral("%1%2").arg(m.level == 2 ? tr("Error: ") : m.level == 1 ? tr("Warning: ") : QString(),
                                                            QString::fromUtf8(m.text)));
    }
    if (!res->ok()) {
        if (!res->parsed && !res->output.trimmed().isEmpty())
            m_outputPane->appendLine(res->output.trimmed());
        if (requester)
            QMessageBox::warning(this, what, res->error());
        else
            statusBar()->showMessage(res->error(), 6000);
    }
}

void MainWindow::vcsApplyStatus(const VcsResultPtr &status)
{
    if (!status->parsed || !status->r.haveHead)
        return;
    m_vcsStatus = status;
    m_vcsDock->showStatus(&status->r);
    QHash<QString, QChar> states;
    if (m_project) {
        for (size_t i = 0; i < status->r.changeCount; ++i) {
            const QString rel = QString::fromUtf8(status->r.changes[i].path);
            const char st = aide_vcs_file_state(&status->r, status->r.changes[i].path, 0);
            if (st) states.insert(QDir::cleanPath(m_project->absolutePath(rel)), QChar(st));
        }
    }
    m_projectTree->setVcsStates(states);
}

void MainWindow::vcsOnFinished(int opInt, VcsResultPtr res)
{
    const auto op = static_cast<AideVcsOp>(opInt);
    if (aide_vcs_op_writes(op)) {
        vcsSetLocked(false);
        statusBar()->clearMessage();
    }

    switch (op) {
    case AIDE_VCS_VERSION:
        return;   // toolChecked() handles it
    case AIDE_VCS_STATUS:
        if (res->ok()) vcsApplyStatus(res);
        else vcsReport(res, opName(op), false);
        return;
    case AIDE_VCS_LOG:
        if (res->ok()) m_historyDock->showLog(res);
        return;
    case AIDE_VCS_DIFF:
        if (res->ok()) vcsShowDiff(res);
        else vcsReport(res, opName(op), true);
        return;
    case AIDE_VCS_BRANCHES: {
        if (!res->ok()) { vcsReport(res, opName(m_vcsPendingBranchOp), true); return; }
        QStringList names;
        for (size_t i = 0; i < res->r.refs.count; ++i) {
            const QString n = QString::fromUtf8(res->r.refs.items[i]);
            if (!res->r.currentRef || n != QString::fromUtf8(res->r.currentRef)) names << n;
        }
        if (names.isEmpty()) {
            QMessageBox::information(this, opName(m_vcsPendingBranchOp), tr("There is no other branch."));
            return;
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(
            this, opName(m_vcsPendingBranchOp),
            m_vcsPendingBranchOp == AIDE_VCS_MERGE ? tr("Merge which branch into the current one?")
                                                   : tr("Switch to which branch?"),
            names, 0, false, &ok);
        if (!ok || choice.isEmpty()) return;
        if (vcsPrepareTouch(opName(m_vcsPendingBranchOp)))
            vcsStartWrite(m_vcsPendingBranchOp, { choice });
        return;
    }
    default:
        break;
    }

    // amivcs needs a user and password for the remote: ask, run again
    if (aide_vcs_op_network(op) && !res->ok() && res->parsed && res->r.authRequired) {
        if (vcsAskLogin(op, res)) return;
        if (op == AIDE_VCS_CLONE) m_vcsCloneTarget.clear();
        statusBar()->showMessage(tr("%1: cancelled.").arg(opName(op)), 4000);
        return;
    }

    // a first push needs the remote URL
    if (op == AIDE_VCS_PUSH && !res->ok() && aide_vcs_needs_remote_url(&res->r)) {
        bool ok = false;
        const QString url = QInputDialog::getText(
            this, opName(op), tr("This repository has no remote yet. Its URL on the AmiVCS server "
                                 "(https://server/name; the repository is created if it does not exist):"),
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || url.isEmpty()) return;
        if (!aide_vcs_is_url(url.toUtf8().constData())) {
            QMessageBox::warning(this, opName(op), tr("'%1' is not a repository URL (https://server/name).").arg(url));
            return;
        }
        vcsStartNet(AIDE_VCS_PUSH, { url });
        return;
    }

    // a commit without a name for the author
    if (op == AIDE_VCS_COMMIT && !res->ok() && res->error().contains(QStringLiteral("who are you"))) {
        vcsSettings(tr("AmiVCS needs a name (and an e-mail) for the author of your commits. "
                       "Enter them below, then commit again."));
        return;
    }

    // a merge (or pull) that stopped on conflicts: not an error to shout about
    if ((op == AIDE_VCS_MERGE || op == AIDE_VCS_PULL) && !res->ok() && res->parsed
        && res->error().contains(QStringLiteral("conflict(s)"))) {
        vcsReport(res, opName(op), false);
        if (res->r.haveHead) vcsApplyStatus(res);   // the merge state, at once
        vcsReloadAfterTouch();                      // reopens the project: status and log again
        m_vcsDockWidget->show();
        m_vcsDockWidget->raise();
        QMessageBox::information(this, opName(op),
            tr("The merge stopped on conflicts. They are listed in the Version Control panel: open each "
               "file, fix the <<<<<<< / ======= / >>>>>>> blocks and mark it resolved, or take one side "
               "as a whole. Then commit -- or abort the merge."));
        return;
    }

    // writing jobs
    vcsReport(res, opName(op), true);
    if (op == AIDE_VCS_CLONE) {
        const QString target = m_vcsCloneTarget;
        m_vcsCloneTarget.clear();
        if (!res->ok()) return;
        const QStringList aprojs = QDir(target).entryList({ QStringLiteral("*.aproj") }, QDir::Files);
        if (aprojs.isEmpty()) {
            QMessageBox::information(this, opName(op),
                                     tr("Cloned into %1. There is no .aproj there yet -- create a project "
                                        "in that directory to work on it.").arg(target));
            return;
        }
        QString pick = aprojs.first();
        if (aprojs.size() > 1) {
            bool ok = false;
            pick = QInputDialog::getItem(this, opName(op), tr("Open which project?"), aprojs, 0, false, &ok);
            if (!ok) return;
        }
        openProjectAt(QDir(target).filePath(pick));
        return;
    }
    if (op == AIDE_VCS_INIT) {
        vcsProjectChanged();   // shows the lists, offers the ignore lines
        return;
    }
    if (res->parsed && res->r.haveHead)
        vcsApplyStatus(res);   // changing commands report the new status
    if (op == AIDE_VCS_COMMIT && res->ok())
        m_vcsDock->clearMessage();
    if (aide_vcs_op_touches_files(op))
        vcsReloadAfterTouch();
    if ((op == AIDE_VCS_ADD || op == AIDE_VCS_COMMIT) && !(res->parsed && res->r.haveHead))
        m_vcs->run(AIDE_VCS_STATUS);   // the merge state changed
    if (op == AIDE_VCS_COMMIT || op == AIDE_VCS_BRANCH_NEW || op == AIDE_VCS_PUSH || aide_vcs_op_touches_files(op))
        m_vcs->run(AIDE_VCS_LOG);
    if (res->ok())
        statusBar()->showMessage(tr("%1: done.").arg(opName(op)), 4000);
}

// After amivcs rewrote files: the project (sources may differ per branch)
// and every open file come back from disk.
void MainWindow::vcsReloadAfterTouch()
{
    if (!m_project) return;
    const QString path = m_project->filePath();
    if (QFile::exists(path)) {
        openProjectAt(path);   // reopens the files of .aproj.user
    } else {
        m_editorTabs->closeAllTabsWithPrompt();
        m_project.reset();
        m_projectTree->clearProject();
        updateWindowTitle();
        QMessageBox::information(this, tr("Project"), tr("The project file is not part of this branch; "
                                                         "the project was closed."));
        vcsProjectChanged();
    }
}

void MainWindow::vcsCheckIgnore()
{
    if (!m_project) return;
    AideProject p;
    const QByteArray root = m_project->projectRoot().toUtf8();
    QFile f(QDir(m_project->projectRoot()).filePath(QStringLiteral(".amivcsignore")));
    QByteArray text;
    if (f.open(QFile::ReadOnly)) text = f.readAll();
    // the project as core sees it
    aide_project_init(&p);
    if (!aide_project_load(&p, m_project->filePath().toUtf8().constData())) {
        aide_project_free(&p);
        return;
    }
    AideStrList missing;
    aide_strlist_init(&missing);
    aide_vcs_ignore_missing(&p, text.isEmpty() ? nullptr : text.constData(), &missing);
    aide_project_free(&p);
    if (missing.count > 0) {
        QStringList lines;
        for (size_t i = 0; i < missing.count; ++i) lines << QString::fromUtf8(missing.items[i]);
        if (QMessageBox::question(this, tr("Version Control"),
                                  tr("Build output and IDE files of this project are not excluded from "
                                     "version control yet. Add these lines to .amivcsignore?\n\n%1")
                                      .arg(lines.join(QLatin1Char('\n')))) == QMessageBox::Yes)
            aide_vcs_ignore_append(root.constData(), &missing);
    }
    aide_strlist_free(&missing);
}

void MainWindow::vcsInit()
{
    if (!m_project) return;
    vcsStartWrite(AIDE_VCS_INIT);
}

void MainWindow::vcsCommit(const QString &message, bool all)
{
    const bool merging = m_vcsStatus && m_vcsStatus->r.merging;
    if (message.isEmpty() && !merging) {
        QMessageBox::information(this, opName(AIDE_VCS_COMMIT), tr("Enter a commit message first."));
        return;
    }
    if (merging && m_vcsStatus->r.conflictsOpen > 0) {
        QMessageBox::information(this, opName(AIDE_VCS_COMMIT),
                                 tr("%1 conflict(s) of the merge are not resolved yet. Fix them and mark them "
                                    "resolved (or take one side) first.").arg(m_vcsStatus->r.conflictsOpen));
        return;
    }
    if (!merging && m_vcsStatus && aide_vcs_has_theirs(&m_vcsStatus->r)
        && QMessageBox::question(this, opName(AIDE_VCS_COMMIT),
                                 tr("There are still .theirs files from a merge with conflicts. Resolve the "
                                    "conflicts and delete those files before committing.\n\nCommit anyway?"))
               != QMessageBox::Yes)
        return;
    m_editorTabs->saveAllModified();   // commit what the editor shows
    vcsStartWrite(AIDE_VCS_COMMIT, {}, message, all);
}

void MainWindow::vcsAbortMerge()
{
    if (!m_vcsStatus || !m_vcsStatus->r.merging) {
        QMessageBox::information(this, opName(AIDE_VCS_MERGE_ABORT), tr("No merge is in progress."));
        return;
    }
    if (QMessageBox::question(this, opName(AIDE_VCS_MERGE_ABORT),
            tr("Give up merging '%1'?\n\nEvery file the merge changed goes back to how it was before, "
               "including your edits to conflicting files. Files not under version control stay.")
                .arg(QString::fromUtf8(m_vcsStatus->r.mergeLabel ? m_vcsStatus->r.mergeLabel : "")))
        != QMessageBox::Yes)
        return;
    if (!m_editorTabs->saveAllModified()) return;
    saveProject();   // the open tabs come back after the reload
    m_vcsDock->clearMessage();
    vcsStartWrite(AIDE_VCS_MERGE_ABORT);
}

// The diff of one file in a window of its own, coloured like the
// AmiVCS GUI (tinted backgrounds work on light and dark palettes).
void MainWindow::vcsShowDiff(const VcsResultPtr &res)
{
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QString title = m_vcsDiffTitle;   // the file the result is about, if it says
    for (size_t i = 0; i < res->r.diffCount; ++i)
        if (res->r.diff[i].op == 'F') { title = QString::fromUtf8(res->r.diff[i].text).remove(QStringLiteral("  (binary)")); break; }
    dlg->setWindowTitle(tr("Diff: %1").arg(title));
    dlg->resize(760, 520);
    auto *layout = new QVBoxLayout(dlg);
    auto *view = new QPlainTextEdit(dlg);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(view);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(buttons);

    QTextCursor c(view->document());
    QTextCharFormat plain, add, del, hunk, note;
    add.setBackground(QColor(40, 170, 60, 60));
    del.setBackground(QColor(220, 50, 50, 60));
    hunk.setForeground(QColor(120, 120, 200));
    note.setFontItalic(true);
    const AideVcsResult &r = res->r;
    bool first = true;
    for (size_t i = 0; i < r.diffCount; ++i) {
        const AideVcsDiffLine &l = r.diff[i];
        const QString t = QString::fromUtf8(l.text ? l.text : "");
        if (l.op == 'F') {
            if (t.endsWith(QStringLiteral("(binary)")))
                { if (!first) c.insertText(QStringLiteral("\n")); c.insertText(tr("(binary file -- no line diff)"), note); first = false; }
            continue;
        }
        if (!first) c.insertText(QStringLiteral("\n"), plain);
        first = false;
        switch (l.op) {
        case '+': c.insertText(QStringLiteral("+") + t, add); break;
        case '-': c.insertText(QStringLiteral("-") + t, del); break;
        case '@': c.insertText(t, hunk); break;
        case '\\': c.insertText(t, note); break;
        default: c.insertText(QStringLiteral(" ") + t, plain); break;
        }
    }
    if (first) c.insertText(tr("(no changes)"), note);
    view->moveCursor(QTextCursor::Start);
    dlg->show();
}

void MainWindow::vcsChooseBranch(AideVcsOp op)
{
    m_vcsPendingBranchOp = op;
    m_vcs->run(AIDE_VCS_BRANCHES);
}

void MainWindow::vcsNewBranch()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, opName(AIDE_VCS_BRANCH_NEW),
                                               tr("Name of the new branch (at the current commit):"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (ok && !name.isEmpty())
        vcsStartWrite(AIDE_VCS_BRANCH_NEW, { name });
}

void MainWindow::vcsPull()
{
    if (vcsTreeDirty()) {
        QMessageBox::information(this, opName(AIDE_VCS_PULL),
                                 tr("There are uncommitted changes. Commit them first, then pull."));
        return;
    }
    if (vcsPrepareTouch(opName(AIDE_VCS_PULL)))
        vcsStartNet(AIDE_VCS_PULL);
}

void MainWindow::vcsPush()
{
    vcsStartNet(AIDE_VCS_PUSH);
}

// Network jobs remember what they were started with: an auth-required
// answer runs them again with the login in front (vcsAskLogin()).
bool MainWindow::vcsStartNet(AideVcsOp op, const QStringList &items, const QString &workDir)
{
    m_vcsNetItems = items;
    m_vcsNetWorkDir = workDir;
    m_vcsLoginTries = 0;
    return vcsStartWrite(op, items, {}, false, workDir);
}

bool MainWindow::vcsAskLogin(AideVcsOp op, const VcsResultPtr &res)
{
    const QString url = QString::fromUtf8(res->r.authUrl ? res->r.authUrl : "");
    VcsLoginDialog dlg(url, QString::fromUtf8(res->r.authUser ? res->r.authUser : ""), m_vcsLoginTries > 0, this);
    if (dlg.exec() != QDialog::Accepted || dlg.password().isEmpty())
        return false;
    ++m_vcsLoginTries;
    AideStrList items, withLogin;
    aide_strlist_init(&items);
    for (const QString &s : m_vcsNetItems) aide_strlist_push(&items, s.toUtf8().constData());
    aide_vcs_items_with_login(&items, dlg.user().toUtf8().constData(), dlg.password().toUtf8().constData(),
                              dlg.remember() ? 1 : 0, &withLogin);
    QStringList args;
    for (size_t i = 0; i < withLogin.count; ++i) args << QString::fromUtf8(withLogin.items[i]);
    aide_strlist_free(&items);
    aide_strlist_free(&withLogin);
    return vcsStartWrite(op, args, {}, false, m_vcsNetWorkDir);
}

// remote URL, user, name/e-mail: the repository's and the defaults
void MainWindow::vcsSettings(const QString &intro)
{
    const QString root = m_project && aide_vcs_present(m_project->projectRoot().toUtf8().constData())
                             ? m_project->projectRoot() : QString();
    VcsSettingsDialog dlg(AppSettings::instance().amivcsProgram(), root, intro, this);
    dlg.exec();
}

void MainWindow::vcsClone()
{
    bool ok = false;
    const QString url = QInputDialog::getText(this, opName(AIDE_VCS_CLONE),
                                              tr("URL of the repository on the AmiVCS server "
                                                 "(https://server/name; user and password are asked for):"),
                                              QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || url.isEmpty()) return;
    const QByteArray url8 = url.toUtf8();
    if (!aide_vcs_is_url(url8.constData())) {
        QMessageBox::warning(this, opName(AIDE_VCS_CLONE), tr("'%1' is not a repository URL (https://server/name).").arg(url));
        return;
    }
    char *repoName = aide_vcs_url_name(url8.constData());
    const QString repo = QString::fromUtf8(repoName);
    free(repoName);
    const QString parent = QFileDialog::getExistingDirectory(this, tr("Clone into which directory?"));
    if (parent.isEmpty()) return;
    const QString name = QInputDialog::getText(this, opName(AIDE_VCS_CLONE), tr("Name of the new folder:"),
                                               QLineEdit::Normal, repo, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (QDir(parent).exists(name)) {
        QMessageBox::warning(this, opName(AIDE_VCS_CLONE), tr("%1 already exists.").arg(QDir(parent).filePath(name)));
        return;
    }
    m_vcsCloneTarget = QDir(parent).filePath(name);
    if (!vcsStartNet(AIDE_VCS_CLONE, { url, name }, parent))
        m_vcsCloneTarget.clear();
}
