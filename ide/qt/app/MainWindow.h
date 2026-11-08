#pragma once

#include "project/Project.h"
#include "project/AddFileDialog.h"
#include "panes/Issue.h"
#include "index/Symbol.h"
#include "index/SymbolDatabase.h"
#include "index/IncludeGraph.h"
#include "index/SfdIndex.h"
#include "help/AutodocIndex.h"
#include "help/AceQuickRef.h"

#include <QList>
#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <memory>

#include "vcs/VcsRunner.h"

class EditorTabWidget;
class EditorWidget;
class ProjectTreeDock;
class SymbolsDock;
class OutputPane;
class IssuesPane;
class HelpDock;
class SearchResultsDock;
class MemoryMapDock;
class QDockWidget;
class QMenu;
class BuildManager;
class EmulatorRunner;
class SymbolIndexer;
class VcsDock;
class HistoryDock;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void newFile();
    void openFile();
    void gotoLocation(const QString &file, int line, int column = 0);
    void showHelpForWord(const QString &word);
    void buildProject();
    void cleanProject();
    void runProject();
    void stopBuild();
    void onBuildStarted();
    void onBuildOutputLine(const QString &line);
    void onBuildIssueFound(const Issue &issue);
    void onBuildFinished(bool success, bool stopped);

    void newProject();
    void openProject();
    void openProjectAt(const QString &path);
    void saveProject();
    void openProjectSettings();
    void openVcsGui();
    void openGlobalSettings();
    void showAboutDialog();
    void focusAutodocSearch();
    void focusProjectSearch();
    void formatCurrentFile();
    void disassembleBinary();
    void addExistingFileToProject();
    void addNewFileToProject(AddFileDialog::FileCategory category);
    void removeFileFromProject(const QString &path);

    void onGotoDefinitionRequested(const QString &symbol, int fromLine);
    void onFileIndexed(const QString &path, QList<Symbol> symbols, QStringList resolvedIncludes);
    void reindexOpenFile(const QString &path);
    void onSymbolActivated(const QString &file, int line);
    void onAutodocRequested(const QString &functionName);
    void onAutodocSearchRequested(const QString &word);
    void onFindReferencesRequested(const QString &word);
    void onSignatureHelpRequested(const QString &word);
    void onStructInfoRequested(const QString &word);

signals:
    // Connected (Qt::AutoConnection -> queued, cross-thread) to m_indexer's
    // slots; emitting these is the only way the UI thread talks to it.
    void requestSetIndexerContext(const QString &projectRoot, const QStringList &includePaths,
                                  ProjectLanguage language);
    void requestIndexFile(const QString &absolutePath);

private:
    void createActions();
    void createDocks();
    void openFileAt(const QString &path, int line = -1, int column = 0);
    void updateWindowTitle();
    void reportToolchainState();
    void loadRecentProjectsMenu();
    void reindexAllProjectFiles();
    void refreshSymbolsDock();
    void refreshCompletionWords();
    int scanEnclosingAsmLabel(const QString &filePath, int fromLine) const;
    // Every directory the real vc/vasm invocations would search: project
    // include paths, the toolchain's own base C include dir (stdio.h etc.),
    // and all active SDKs' C + asm include dirs (NDK/MUI/...).
    QStringList allIncludeSearchPaths() const;
    // If `line` in `editor` is a #include/include directive, resolves and
    // opens the target file (or reports why it couldn't) and returns true --
    // the caller should skip the normal symbol-lookup chain in that case.
    bool tryOpenIncludeOnLine(EditorWidget *editor, int line);
    // Saves every modified open editor (see EditorTabWidget::saveAllModified())
    // before build()/build()-then-run -- shared by buildProject() and
    // runProject() so both always compile what's actually in the editor.
    void saveEditorsBeforeBuild();
    void setBuildActionsEnabled(bool enabled);

    // ---- version control (MainWindowVcs.cpp) ----
    void createVcs();                 // docks, menu, runner
    void vcsProjectChanged();         // after a project opened/closed
    void vcsRefresh(bool withLog = false);
    void vcsRefreshSoon();            // debounced, after saves/focus
    bool vcsStartWrite(AideVcsOp op, const QStringList &items = {}, const QString &message = {},
                       bool flag = false, const QString &workDir = {});
    bool vcsPrepareTouch(const QString &what);  // save editors + project, ask
    void vcsSetLocked(bool locked);
    void vcsOnToolChecked(bool available, const QString &problem);
    void vcsOnStarted(int op);
    void vcsOnFinished(int op, VcsResultPtr result);
    void vcsApplyStatus(const VcsResultPtr &status);
    void vcsReport(const VcsResultPtr &result, const QString &what, bool requester);
    void vcsCheckIgnore();
    void vcsInit();
    void vcsCommit(const QString &message, bool all);
    void vcsChooseBranch(AideVcsOp op);
    void vcsNewBranch();
    void vcsPull();
    void vcsPush();
    void vcsClone();
    void vcsSettings(const QString &intro = {});
    bool vcsStartNet(AideVcsOp op, const QStringList &items = {}, const QString &workDir = {});
    bool vcsAskLogin(AideVcsOp op, const VcsResultPtr &res);
    void vcsReloadAfterTouch();
    bool vcsTreeDirty() const;
    void vcsAbortMerge();
    void vcsShowDiff(const VcsResultPtr &res);

    EditorTabWidget *m_editorTabs;
    ProjectTreeDock *m_projectTree;
    SymbolsDock *m_symbolsDock;
    OutputPane *m_outputPane;
    IssuesPane *m_issuesPane;
    HelpDock *m_helpDock;
    SearchResultsDock *m_searchResultsDock;
    MemoryMapDock *m_memoryMapDock;

    QDockWidget *m_projectTreeDockWidget;
    QDockWidget *m_symbolsDockWidget;
    QDockWidget *m_bottomDockWidget;
    QDockWidget *m_helpDockWidget;
    QDockWidget *m_searchResultsDockWidget;
    QDockWidget *m_memoryMapDockWidget;

    QMenu *m_recentProjectsMenu = nullptr;
    std::unique_ptr<Project> m_project;
    BuildManager *m_buildManager;
    EmulatorRunner *m_emulatorRunner;
    QList<Issue> m_currentIssues;
    bool m_runAfterBuild = false;
    bool m_building = false;
    QAction *m_buildAct = nullptr;
    QAction *m_cleanAct = nullptr;
    QAction *m_runAct = nullptr;
    QAction *m_stopBuildAct = nullptr;

    QThread m_indexerThread;
    SymbolIndexer *m_indexer;
    SymbolDatabase m_symbolDatabase;
    IncludeGraph m_includeGraph;
    SfdIndex m_sfdIndex;
    AutodocIndex m_autodocIndex;
    AceQuickRef m_aceQuickRef;
    QStringList m_completionWords;
    // Filled by AppSettings::migrateStaleSettings() at startup, shown once.
    QStringList m_settingsMigrations;

    VcsRunner *m_vcs = nullptr;
    VcsDock *m_vcsDock = nullptr;
    HistoryDock *m_historyDock = nullptr;
    QDockWidget *m_vcsDockWidget = nullptr;
    QDockWidget *m_historyDockWidget = nullptr;
    QList<QAction *> m_vcsRepoActions;   // need a repository
    QAction *m_vcsInitAct = nullptr;
    QAction *m_vcsCloneAct = nullptr;
    QAction *m_vcsCancelAct = nullptr;
    VcsResultPtr m_vcsStatus;            // the newest status
    QTimer m_vcsRefreshTimer;
    AideVcsOp m_vcsPendingBranchOp = AIDE_VCS_SWITCH;
    QString m_vcsCloneTarget;            // directory a running clone writes
    QStringList m_vcsNetItems;           // the last network job, for a retry after a login
    QString m_vcsNetWorkDir;
    int m_vcsLoginTries = 0;
    bool m_vcsLocked = false;
    QString m_vcsDiffTitle;              // file of the diff job running
};
