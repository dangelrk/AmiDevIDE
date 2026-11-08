#pragma once

// "Version Control" dock: branch, changes not staged / staged, commit.
// Shows what the last amivcs status said; every action goes out as a
// signal, MainWindow runs it through VcsRunner.

#include "VcsRunner.h"

#include <QWidget>

class QCheckBox;
class QFrame;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

class VcsDock : public QWidget {
    Q_OBJECT
public:
    explicit VcsDock(QWidget *parent = nullptr);

    // What the dock shows instead of the lists
    void showUnavailable(const QString &reason);   // no amivcs
    void showNoProject();
    void showNoRepository();                      // project without .amivcs
    void showStatus(const AideVcsResult *status);  // a status result
    void setBusy(bool busy);                       // a writing job runs
    void clearMessage();

signals:
    void refreshRequested();
    void initRequested();
    void stageRequested(const QStringList &paths);
    void unstageRequested(const QStringList &paths);
    void stageAllRequested();
    void commitRequested(const QString &message, bool all);
    void fileActivated(const QString &relPath);
    void diffRequested(const QString &relPath, bool staged);
    void abortMergeRequested();
    void resolveRequested(const QStringList &paths, bool theirs);
    void markResolvedRequested(const QStringList &paths);

private:
    QStringList selectedPaths(QListWidget *list) const;

    QStackedWidget *m_stack;
    QLabel *m_info;
    QPushButton *m_initButton;
    QLabel *m_branch;
    QListWidget *m_unstaged;
    QListWidget *m_staged;
    QPushButton *m_stage, *m_stageAll, *m_unstage, *m_commit, *m_refresh;
    QPlainTextEdit *m_message;
    QFrame *m_mergeBox;
    QLabel *m_mergeLabel;
    QListWidget *m_conflicts;
    QPushButton *m_abortMerge, *m_takeOurs, *m_takeTheirs, *m_resolved, *m_diff;
    bool m_merging = false;
    QListWidget *m_lastList = nullptr;   // the change list clicked last (Diff)
    int m_conflictsOpen = 0;
    QCheckBox *m_all;
    bool m_busy = false;
};
