#pragma once

// "History" dock: amivcs log --all as a table with the branch graph
// (lanes from aide_vcs_graph(), drawn like AmiVCS's own Qt GUI).

#include "VcsRunner.h"

#include <QWidget>

class QTableWidget;

class HistoryDock : public QWidget {
    Q_OBJECT
public:
    explicit HistoryDock(QWidget *parent = nullptr);
    ~HistoryDock() override;

    void showLog(VcsResultPtr log);
    void clear();

    // used by the graph delegate
    const AideVcsGraphRow *graphRow(int row) const;
    int laneCount() const { return m_maxLanes; }

private:
    QTableWidget *m_table;
    VcsResultPtr m_log;
    AideVcsGraphRow *m_rows = nullptr;
    int m_maxLanes = 0;
};
