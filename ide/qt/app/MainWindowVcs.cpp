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
    m_vcsRepoActions << menu->addAction(tr("Restore &Deleted Files"), this, [this] {
        if (vcsPrepareTouch(tr("Restore the committed versions of deleted files")))
            vcsStartWrite(AIDE_VCS_CHECKOUT);
    });
    menu->addSeparator();
    m_vcsRepoActions << menu->addAction(tr("P&ull"), this, &MainWindow::vcsPull);
    m_vcsRepoActions << menu->addAction(tr("&Push"), this, &MainWindow::vcsPush);
    m_vcsCloneAct = menu->addAction(tr("C&lone..."), this, &MainWindow::vcsClone);
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

    // a first push needs the repository name on the server
    if (op == AIDE_VCS_PUSH && !res->ok() && aide_vcs_needs_remote_name(&res->r)) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, opName(op), tr("This repository has not been pushed yet. Name of the repository "
                                 "on the server (it is created if it does not exist):"),
            QLineEdit::Normal, m_project ? m_project->name : QString(), &ok).trimmed();
        if (ok && !name.isEmpty())
            vcsStartWrite(AIDE_VCS_PUSH, { name });
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
    if (message.isEmpty()) {
        QMessageBox::information(this, opName(AIDE_VCS_COMMIT), tr("Enter a commit message first."));
        return;
    }
    if (m_vcsStatus && aide_vcs_has_theirs(&m_vcsStatus->r)
        && QMessageBox::question(this, opName(AIDE_VCS_COMMIT),
                                 tr("There are still .theirs files from a merge with conflicts. Resolve the "
                                    "conflicts and delete those files before committing.\n\nCommit anyway?"))
               != QMessageBox::Yes)
        return;
    m_editorTabs->saveAllModified();   // commit what the editor shows
    vcsStartWrite(AIDE_VCS_COMMIT, {}, message, all);
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
        vcsStartWrite(AIDE_VCS_PULL);
}

void MainWindow::vcsPush()
{
    vcsStartWrite(AIDE_VCS_PUSH);
}

void MainWindow::vcsClone()
{
    bool ok = false;
    const QString repo = QInputDialog::getText(this, opName(AIDE_VCS_CLONE),
                                               tr("Repository name on the server (server, user and password "
                                                  "come from the AmiVCS configuration):"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || repo.isEmpty()) return;
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
    if (!vcsStartWrite(AIDE_VCS_CLONE, { repo, name }, {}, false, parent))
        m_vcsCloneTarget.clear();
}
