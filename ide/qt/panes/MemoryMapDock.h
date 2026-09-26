#pragma once

#include <QWidget>

class QTreeWidget;
class QLabel;

// Post-build memory-usage overview: per-hunk CODE/DATA/BSS sizes (via
// HunkReader, the same parser the Disassemble feature uses) plus load size
// (CODE+DATA, what actually has to fit/load) and BSS (runtime-only,
// zero-filled) totals. Refreshed automatically after each successful build.
class MemoryMapDock : public QWidget {
    Q_OBJECT
public:
    explicit MemoryMapDock(QWidget *parent = nullptr);

    // Loads `path` via HunkReader and populates the tree + totals. On
    // failure, shows the dock's own inline error state (no dialog -- this
    // runs automatically after every successful build, not on user request).
    void showForBinary(const QString &path);
    void clear();

private:
    QTreeWidget *m_tree;
    QLabel *m_summaryLabel;
};
